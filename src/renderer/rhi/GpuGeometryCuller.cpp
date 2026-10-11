#include "renderer/rhi/GpuGeometryCuller.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <algorithm>
#include <cmath>
namespace render {
namespace {
// Only skip compute when the entire static affine bound is well inside clip.
bool fullyInside(const DrawPacket& packet,const FrameData& frame) {
    if(packet.mesh->indirectBuffer() || packet.model[0].w!=0 || packet.model[1].w!=0 || packet.model[2].w!=0 || packet.model[3].w!=1)return false;
    const auto lo=packet.mesh->boundsMin(),hi=packet.mesh->boundsMax();
    for(int i=0;i<8;++i){
        auto p=glm::dmat4(frame.viewProjection)*glm::dmat4(packet.model)*glm::dvec4(i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z,1);
        double epsilon=std::max({1.,std::abs(p.x),std::abs(p.y),std::abs(p.z),std::abs(p.w)})*1e-5;
        if(!std::isfinite(epsilon) || p.x+p.w<epsilon || p.w-p.x<epsilon || p.y+p.w<epsilon || p.w-p.y<epsilon || p.z<epsilon || p.w-p.z<epsilon)return false;
    }
    return true;
}
struct alignas(16) Parameters {glm::mat4 vp,model;glm::vec4 guard;glm::uvec4 counts;};
static_assert(sizeof(Parameters)==160,"Geometry culling uniform ABI");
}
struct GpuGeometryCuller::Storage {
    Resources resources;
    std::shared_ptr<GpuMesh> mesh;
    rhi::BufferHandle offsets,groups,indices,arguments,dispatch;
    glm::mat4 vp{1},model{1};uint32_t width=0,height=0;uint64_t revision=0;
    bool valid=false,pending=false;
    size_t bytes=0;
    Storage(std::shared_ptr<rhi::GraphicsDevice> d,std::shared_ptr<GpuMesh> source):resources(d),mesh(std::move(source)) {
        using namespace rhi;
        const size_t triangles=mesh->indexCount()/3,groupCount=(triangles+63)/64;
        offsets=resources.buffer({triangles*4,BufferUsage::Storage|BufferUsage::CopyDestination,"Triangle local offsets",BufferInitialization::Uninitialized});
        groups=resources.buffer({groupCount*8,BufferUsage::Storage|BufferUsage::CopyDestination,"Triangle group prefixes",BufferInitialization::Uninitialized});
        indices=resources.buffer({size_t(mesh->indexCount())*4,BufferUsage::Storage|BufferUsage::Index|BufferUsage::CopySource|BufferUsage::CopyDestination,"Visible triangle indices",BufferInitialization::Uninitialized});
        const DrawIndexedIndirectArguments zero{0,1,0,0,0};
        arguments=resources.buffer({20,BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource,"Visible indexed arguments"},&zero);
        const uint32_t empty[3]={0,1,1};
        dispatch=resources.buffer({12,BufferUsage::Storage|BufferUsage::Indirect,"Triangle active dispatch"},empty);
        bytes=triangles*4+groupCount*8+size_t(mesh->indexCount())*4+32;
    }
};
GpuGeometryCuller::GpuGeometryCuller(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory):resources_(d) {
    using namespace rhi;
    const DrawIndexedIndirectArguments zero{0,1,0,0,0};
    emptyArguments_=resources_.buffer({20,BufferUsage::Storage,"Unused cull input arguments"},&zero);
    for(size_t i=0;i<4;++i){
        const auto path=directory+"/geometry-cull-"+std::array<const char*,4>{"prepare","mark","scan","scatter"}[i]+".comp";
        ComputePipelineDesc desc;desc.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};
        const auto reflection=nlohmann::json::parse(readShaderText(desc.shader.reflectionPath));
        desc.threads=reflection.at("entryPoints").at(0).at("workgroup_size").get<std::array<uint32_t,3>>();
        desc.bindings={{0,{}},{1,{}}};desc.label="Geometry culling";
        for(const auto& u:reflection.at("ubos"))desc.bindings[u.at("set").get<size_t>()].entries.push_back({u.at("binding"),BindingType::UniformBuffer,ShaderStage::Compute,u.at("name"),u.at("block_size")});
        for(const auto& b:reflection.at("ssbos"))desc.bindings[b.at("set").get<size_t>()].entries.push_back({b.at("binding"),b.value("readonly",false)?BindingType::StorageRead:b.value("writeonly",false)?BindingType::StorageWrite:BindingType::StorageReadWrite,ShaderStage::Compute,b.at("name"),b.at("block_size")});
        kernels_[i]={resources_.computePipeline(desc),desc.bindings};
    }
}
GpuGeometryCuller::~GpuGeometryCuller()=default;
std::vector<CulledMeshDraw> GpuGeometryCuller::record(Resources& frame,rhi::CommandList& commands,const FrameData& view,const std::vector<DrawPacket>& packets) {
    using namespace rhi;
    candidateTriangles_=0;allocatedBytes_=0;cacheHits_=0;dispatches_=0;slots_.resize(packets.size());
    std::vector<CulledMeshDraw> draws(packets.size());
    const auto& limits=resources_.device->computeLimits();
    for(size_t i=0;i<packets.size();++i){
        const auto& packet=packets[i];auto& slot=slots_[i];const auto mesh=packet.mesh;
        const auto triangles=mesh->indexCount()/3,groupCount=(triangles+63)/64;
        const size_t vertexBytes=size_t(mesh->vertexCount())*32,indexBytes=size_t(mesh->indexCount())*4;
        if(!view.triangleCulling || (view.clusters.enabled&&packet.clusters) || packet.wireframe || packet.material->transparent() || packet.material->extension().lobes.w!=0 || mesh->instances() ||
           !mesh->ready() || triangles<view.triangleCullMinimum || groupCount>limits.maxGroups[0] ||
           vertexBytes>limits.maxStorageRange || indexBytes>limits.maxStorageRange || fullyInside(packet,view)) {
            slot.reset();continue;
        }
        if(!slot || slot->mesh!=mesh){
            slot.reset();
            const auto memory=resources_.device->resourceMemory();
            const size_t required=indexBytes+size_t(triangles)*4+size_t(groupCount)*8+32;
            if(memory.budgetBytes && required>memory.budgetBytes-std::min(memory.budgetBytes,memory.usedBytes()))continue;
            try {slot=std::make_unique<Storage>(resources_.device,mesh);}catch(const ResourceBudgetExceeded&){continue;}
        }
        candidateTriangles_+=triangles;allocatedBytes_+=slot->bytes;
        slot->pending=false;
        if(!mesh->indirectBuffer() && slot->valid && slot->vp==view.viewProjection && slot->model==packet.model &&
           slot->width==view.viewportWidth && slot->height==view.viewportHeight && slot->revision==mesh->geometryRevision()){
            ++cacheHits_;draws[i]={slot->indices,slot->arguments};continue;
        }
        slot->valid=false;slot->pending=true;slot->vp=view.viewProjection;slot->model=packet.model;
        slot->width=view.viewportWidth;slot->height=view.viewportHeight;slot->revision=mesh->geometryRevision();
        Parameters parameters{view.viewProjection,packet.model,{1+4.f/view.viewportWidth,1+4.f/view.viewportHeight,1e-5f,0},
                              {triangles,mesh->vertexCount(),mesh->indirectBuffer()?1u:0u,groupCount}};
        auto uniform=frame.buffer({sizeof(parameters),BufferUsage::Uniform,"Triangle cull parameters"},&parameters);
        for(size_t stage=0;stage<4;++stage){
            if(stage==0 && !mesh->indirectBuffer())continue;
            auto& kernel=kernels_[stage];std::vector<BindingSetHandle> sets;
            for(const auto& layout:kernel.layouts){
                std::vector<BindingEntry> entries;
                for(const auto& binding:layout.entries){
                    if(binding.type==BindingType::UniformBuffer){entries.push_back({binding.binding,uniform,0,sizeof(parameters),{},{}});continue;}
                    BufferHandle buffer;size_t bytes=0;
                    switch(binding.binding){
                    case 0:buffer=mesh->vertexBuffer();bytes=vertexBytes;break;
                    case 1:buffer=mesh->indexBuffer();bytes=indexBytes;break;
                    case 2:buffer=mesh->indirectBuffer()?mesh->indirectBuffer():emptyArguments_;bytes=20;break;
                    case 3:buffer=slot->offsets;bytes=size_t(triangles)*4;break;
                    case 4:buffer=slot->groups;bytes=size_t(groupCount)*8;break;
                    case 5:buffer=slot->indices;bytes=indexBytes;break;
                    case 6:buffer=slot->arguments;bytes=20;break;
                    case 7:buffer=slot->dispatch;bytes=12;break;
                    default:throw std::logic_error("Unknown geometry cull binding");
                    }
                    entries.push_back({binding.binding,buffer,0,bytes,{},{}});
                }
                sets.push_back(frame.bindings({layout,entries}));
            }
            commands.setLabel(std::string("geometry-cull/")+std::array<const char*,4>{"prepare","mark","scan","scatter"}[stage]);
            if((stage==1 || stage==3) && mesh->indirectBuffer())commands.dispatchIndirect(kernel.pipeline,sets,slot->dispatch);
            else commands.dispatch(kernel.pipeline,sets,{stage==0 || stage==2?1u:groupCount,1,1});
            ++dispatches_;
        }
        draws[i]={slot->indices,slot->arguments};
    }
    return draws;
}
void GpuGeometryCuller::commit(){for(auto& slot:slots_)if(slot && slot->pending){slot->valid=true;slot->pending=false;}}
std::vector<TriangleCullCounts> GpuGeometryCuller::readCounts() {
    std::vector<TriangleCullCounts> counts;
    for(size_t i=0;i<slots_.size();++i)if(const auto& slot=slots_[i]){
        rhi::DrawIndexedIndirectArguments output{},input{slot->mesh->indexCount(),1,0,0,0};
        resources_.device->readBuffer(slot->arguments,0,20,&output);
        if(slot->mesh->indirectBuffer())resources_.device->readBuffer(slot->mesh->indirectBuffer(),0,20,&input);
        counts.push_back({i,input.indexCount/3,output.indexCount/3});
    }
    return counts;
}
}
