#include "renderer/rhi/GpuClusterCuller.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <algorithm>
#include <cmath>
namespace render {
namespace {
struct alignas(16) Parameters {glm::mat4 vp,view;glm::vec4 projection,guard;glm::uvec4 counts;glm::vec4 rounding;};
static_assert(sizeof(Parameters)==192,"Cluster parameter ABI");
float scaleBound(const glm::mat4& m){float one=0,inf=0;for(int i=0;i<3;++i){float col=0,row=0;for(int j=0;j<3;++j){col+=std::abs(m[i][j]);row+=std::abs(m[j][i]);}one=std::max(one,col);inf=std::max(inf,row);}return std::sqrt(one*inf);}
}
struct GpuClusterCuller::Slot {
    Resources resources;std::shared_ptr<GpuClusterGeometry> geometry;std::shared_ptr<GpuMesh> mesh;
    rhi::BufferHandle offsets,groups,indices,args;size_t bytes=0;
    Slot(std::shared_ptr<rhi::GraphicsDevice> d,const DrawPacket& p):resources(d),geometry(p.clusters),mesh(p.mesh){
        using namespace rhi;size_t count=geometry->hierarchy->clusters.size(),groupCount=(count+63)/64;
        offsets=resources.buffer({count*8,BufferUsage::Storage|BufferUsage::CopyDestination,"Cluster output prefixes",BufferInitialization::Uninitialized});
        groups=resources.buffer({groupCount*8,BufferUsage::Storage|BufferUsage::CopyDestination,"Cluster group prefixes",BufferInitialization::Uninitialized});
        indices=resources.buffer({size_t(geometry->hierarchy->sourceIndices)*4,BufferUsage::Storage|BufferUsage::Index|BufferUsage::CopySource|BufferUsage::CopyDestination,"Selected cluster indices",BufferInitialization::Uninitialized});
        const uint32_t empty[6]={0,1,0,0,0,0};args=resources.buffer({24,BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource,"Cluster indexed arguments and count"},empty);
        bytes=count*8+groupCount*8+size_t(geometry->hierarchy->sourceIndices)*4+24;
    }
};
GpuClusterCuller::GpuClusterCuller(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory):directory_(directory),resources_(d){
    for(size_t i=0;i<3;++i){auto path=directory+"/cluster-"+std::array<const char*,3>{"mark","scan","scatter"}[i]+".comp";
        rhi::ComputePipelineDesc desc;desc.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};
        auto reflection=nlohmann::json::parse(rhi::readShaderText(desc.shader.reflectionPath));
        desc.threads=reflection.at("entryPoints").at(0).at("workgroup_size").get<std::array<uint32_t,3>>();desc.bindings={{0,{}},{1,{}}};desc.label="Hierarchical cluster selection";
        for(const auto& u:reflection.at("ubos"))desc.bindings[u.at("set").get<size_t>()].entries.push_back({u.at("binding"),rhi::BindingType::UniformBuffer,rhi::ShaderStage::Compute,u.at("name"),u.at("block_size")});
        for(const auto& b:reflection.at("ssbos"))desc.bindings[b.at("set").get<size_t>()].entries.push_back({b.at("binding"),b.value("readonly",false)?rhi::BindingType::StorageRead:b.value("writeonly",false)?rhi::BindingType::StorageWrite:rhi::BindingType::StorageReadWrite,rhi::ShaderStage::Compute,b.at("name"),b.at("block_size")});
        kernels_[i]={resources_.computePipeline(desc),desc.bindings};
    }
}
GpuClusterCuller::~GpuClusterCuller()=default;
std::vector<CulledMeshDraw> GpuClusterCuller::record(Resources& frame,rhi::CommandList& commands,const FrameData& view,const std::vector<DrawPacket>& packets,std::vector<CulledMeshDraw> draws,bool twoPhase,rhi::TextureViewHandle history,uint32_t levels,const glm::mat4& historyVP){
    using namespace rhi;committed_=false;stats_={};slots_.resize(packets.size());const auto limits=resources_.device->computeLimits();
    if(view.clusters.batched&&recordBatched(frame,commands,view,packets,draws,history,levels,twoPhase?2u:0u,historyVP)){slots_.clear();return draws;}
    batch_.reset();
    struct Active {size_t packet;rhi::BufferHandle uniform;};std::vector<Active> active;
    auto projection=view.viewProjection*glm::inverse(view.view);float focal=std::max(std::abs(projection[0][0])*view.viewportWidth,std::abs(projection[1][1])*view.viewportHeight)*.5f;
    for(size_t i=0;i<packets.size();++i){const auto& p=packets[i];auto& slot=slots_[i];
        if(!p.clusters||p.mesh->instances()||!p.mesh->ready()||p.mesh->indirectBuffer()||(p.wireframe&&!view.clusters.wireframe)||p.material->transparent()||p.material->extension().lobes.w!=0){slot.reset();continue;}
        const auto& h=*p.clusters->hierarchy;size_t count=h.clusters.size(),groups=(count+63)/64;
        if(!count||count>limits.maxGroups[0]||h.nodes.size()*48>limits.maxStorageRange||count*48>limits.maxStorageRange||h.indices.size()*4>limits.maxStorageRange){slot.reset();continue;}
        if(!slot||slot->geometry!=p.clusters||slot->mesh!=p.mesh){slot.reset();try{slot=std::make_unique<Slot>(resources_.device,p);}catch(const ResourceBudgetExceeded&){continue;}}
        bool allowLod=p.material->parameters().factors.w==0&&p.material->parameters().albedoAlpha.w>=1&&p.material->extension().settings.w==0&&p.material->extension().settings.z==0;
        Parameters params{view.viewProjection*p.model,view.view*p.model,{focal,scaleBound(p.model),view.nearPlane,view.clusters.pixelError},
            {1+4.f/view.viewportWidth,1+4.f/view.viewportHeight,std::abs(projection[2][3])<1e-5f?1.f:0.f,allowLod?1.f:0.f},
            {uint32_t(count),uint32_t(groups),h.sourceIndices,view.objectCulling?1u:0u}};
        const auto magnitude=glm::vec4(glm::max(glm::abs(p.mesh->boundsMin()),glm::abs(p.mesh->boundsMax())),1);
        glm::vec4 worldMagnitude(0),rounding(0);
        for(int k=0;k<4;++k)worldMagnitude+=glm::abs(p.model[k])*magnitude[k];
        for(int k=0;k<4;++k)rounding+=glm::abs(view.viewProjection[k])*worldMagnitude[k];
        params.rounding=rounding*4e-6f+glm::vec4(1e-7f);
        auto uniform=frame.buffer({sizeof(Parameters),BufferUsage::Uniform,"Cluster selection view"},&params);
        active.push_back({i,uniform});
        draws[i]={slot->indices,slot->args};++stats_.packets;stats_.sourceTriangles+=h.sourceIndices/3;stats_.bufferBytes+=slot->bytes;
    }
    // Stage-major recording groups identical pipelines. Each packet retains its
    // own prefix/output range; native encoder batching remains backend work.
    for(size_t stage=0;stage<3&&!active.empty();++stage){
        const auto& kernel=kernels_[stage];
        commands.setLabel(std::string("clusters/")+std::array<const char*,3>{"mark","scan","scatter"}[stage]);
        for(const auto& item:active){const auto& p=packets[item.packet];const auto& slot=slots_[item.packet];
            const auto& h=*p.clusters->hierarchy;size_t count=h.clusters.size(),groups=(count+63)/64;
            auto uniform=item.uniform;std::vector<BindingSetHandle> sets;
            for(const auto& layout:kernel.layouts){std::vector<BindingEntry> entries;
                for(const auto& b:layout.entries){if(b.type==BindingType::UniformBuffer){entries.push_back({b.binding,uniform,0,sizeof(Parameters),{},{}});continue;}
                    BufferHandle buffer;size_t bytes=0;switch(b.binding){
                    case 0:buffer=p.clusters->nodes;bytes=h.nodes.size()*48;break;
                    case 1:buffer=p.clusters->clusters;bytes=count*48;break;
                    case 2:buffer=p.clusters->indices;bytes=h.indices.size()*4;break;
                    case 3:buffer=slot->offsets;bytes=count*8;break;
                    case 4:buffer=slot->groups;bytes=groups*8;break;
                    case 5:buffer=slot->indices;bytes=size_t(h.sourceIndices)*4;break;
                    case 6:buffer=slot->args;bytes=24;break;
                    default:throw std::logic_error("Unknown cluster binding");}
                    entries.push_back({b.binding,buffer,0,bytes,{},{}});
                }sets.push_back(frame.bindings({layout,entries}));
            }
            commands.dispatch(kernel.pipeline,sets,{uint32_t(stage==0?groups:stage==1?1:count),1,1});++stats_.dispatches;
        }
    }

    return draws;
}
void GpuClusterCuller::commit(){committed_=true;}
std::vector<ClusterCullCount> GpuClusterCuller::readCounts(){std::vector<ClusterCullCount> out;if(!committed_)return out;if(stats_.batched)return readBatchCounts();
    for(size_t i=0;i<slots_.size();++i)if(auto& s=slots_[i]){uint32_t args[6];resources_.device->readBuffer(s->args,0,24,args);out.push_back({i,s->geometry->hierarchy->sourceIndices/3,args[0]/3,args[5]});}return out;}
std::vector<uint32_t> GpuClusterCuller::readIndices(size_t packet){if(!committed_)return {};if(stats_.batched)return readBatchIndices(packet);if(packet>=slots_.size()||!slots_[packet])return {};auto& s=slots_[packet];uint32_t count=0;resources_.device->readBuffer(s->args,0,4,&count);
    std::vector<uint32_t> out(count);if(count)resources_.device->readBuffer(s->indices,0,size_t(count)*4,out.data());return out;}
}
