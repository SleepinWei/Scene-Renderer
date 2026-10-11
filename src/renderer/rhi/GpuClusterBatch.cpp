#include "renderer/rhi/GpuClusterCuller.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <map>
namespace render {
namespace {
struct alignas(16) BatchMesh {
    glm::mat4 vp{1},view{1};glm::vec4 projection{0},guard{0};glm::uvec4 counts{0};glm::vec4 rounding{0};
    glm::uvec4 ranges{0};glm::mat4 historyVP{1};glm::vec4 historyRounding{0}; // cluster start, group start, output start, argument word start.
};
static_assert(sizeof(BatchMesh)==288,"Cluster batch std430 ABI");
bool compatible(const DrawPacket& p,const FrameData& f){return p.clusters&&p.mesh->ready()&&!p.mesh->instances()&&!p.mesh->indirectBuffer()&&
    (!p.wireframe||f.clusters.wireframe)&&!p.material->transparent()&&p.material->extension().lobes.w==0;}
float bound(const glm::mat4& m){float one=0,inf=0;for(int i=0;i<3;++i){float a=0,b=0;for(int j=0;j<3;++j){a+=std::abs(m[i][j]);b+=std::abs(m[j][i]);}one=std::max(one,a);inf=std::max(inf,b);}return std::sqrt(one*inf);}
}
struct GpuClusterCuller::Batch {
    struct Entry {uint64_t id;std::shared_ptr<GpuClusterGeometry> geometry;std::shared_ptr<GpuMesh> mesh;glm::uvec4 ranges;uint32_t padded;};
    Resources resources;std::vector<Entry> entries;std::vector<std::pair<size_t,size_t>> active;
    std::array<Kernel,4> kernels;
    rhi::BufferHandle nodes,clusters,source,offsets,groups,output,args;
    rhi::TextureViewHandle emptyDepth;rhi::SamplerHandle sampler;
    uint32_t clusterCount=0,groupCount=0,outputCount=0;size_t bytes=0,nodeBytes=0,sourceBytes=0;
    Batch(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory,const std::vector<DrawPacket>& packets,const FrameData& f):resources(d){
        using namespace rhi;
        const auto nativeLimits=d->computeLimits();size_t nodeN=0,clusterN=0,indexN=0,outputN=0,meshN=0;
        std::set<const ClusterHierarchy*> unique;
        for(const auto& p:packets)if(compatible(p,f)){const auto& h=*p.clusters->hierarchy;if(h.clusters.empty())continue;
            if(unique.insert(&h).second){nodeN+=h.nodes.size();indexN+=h.indices.size();}clusterN+=(h.clusters.size()+63)/64*64;outputN+=h.sourceIndices;++meshN;}
        if(!meshN||nodeN>UINT32_MAX||clusterN>UINT32_MAX||(clusterN+63)/64>nativeLimits.maxGroups[0]||(clusterN+nativeLimits.maxGroups[0]-1)/nativeLimits.maxGroups[0]>nativeLimits.maxGroups[1]||meshN>nativeLimits.maxGroups[0]||indexN>UINT32_MAX||outputN>UINT32_MAX||
           nodeN*48>nativeLimits.maxStorageRange||clusterN*48>nativeLimits.maxStorageRange||indexN*4>nativeLimits.maxStorageRange||
           outputN*4>nativeLimits.maxStorageRange||meshN*288>nativeLimits.maxStorageRange||clusterN*16>nativeLimits.maxStorageRange||meshN*48>nativeLimits.maxStorageRange)
            throw ResourceBudgetExceeded("Cluster batch exceeds native storage/dispatch limits");
        std::vector<ClusterNode> allNodes;std::vector<GeometryCluster> allClusters;std::vector<uint32_t> indices;
        std::map<const ClusterHierarchy*,std::pair<uint32_t,uint32_t>> immutableRanges;
        for(const auto& p:packets)if(compatible(p,f)){
            const auto& h=*p.clusters->hierarchy;if(h.clusters.empty())continue;
            auto geometry=immutableRanges.emplace(&h,std::make_pair(uint32_t(allNodes.size()),uint32_t(indices.size())));
            uint32_t owner=uint32_t(entries.size()),nodeBase=geometry.first->second.first,indexBase=geometry.first->second.second;
            uint32_t padded=uint32_t((h.clusters.size()+63)/64*64);glm::uvec4 ranges(clusterCount,groupCount,outputCount,owner*12);
            entries.push_back({p.id,p.clusters,p.mesh,ranges,padded});
            if(geometry.second){for(auto n:h.nodes){for(int k=0;k<3;++k)if(n.links[k]!=UINT32_MAX)n.links[k]+=nodeBase;allNodes.push_back(n);}
                indices.insert(indices.end(),h.indices.begin(),h.indices.end());}
            for(auto c:h.clusters){c.range.x+=indexBase;c.range.z+=nodeBase;c.range.w=owner;allClusters.push_back(c);}
            while(allClusters.size()<size_t(clusterCount)+padded){GeometryCluster empty;empty.range.w=owner;allClusters.push_back(empty);}
            clusterCount+=padded;groupCount+=padded/64;outputCount+=h.sourceIndices;
        }
        nodeBytes=allNodes.size()*48;sourceBytes=indices.size()*4;
        nodes=resources.buffer({allNodes.size()*48,BufferUsage::Storage,"Cluster batch nodes"},allNodes.data());
        clusters=resources.buffer({allClusters.size()*48,BufferUsage::Storage,"Cluster batch meshlets"},allClusters.data());
        source=resources.buffer({indices.size()*4,BufferUsage::Storage,"Cluster batch source indices"},indices.data());
        auto scratch=[&](size_t size,BufferUsage usage,const char* label){return resources.buffer({size,usage|BufferUsage::Storage|BufferUsage::CopyDestination,label,BufferInitialization::Uninitialized});};
        offsets=scratch(size_t(clusterCount)*16,BufferUsage::CopySource,"Cluster batch meshlet prefixes");
        groups=scratch(size_t(groupCount)*8,BufferUsage::None,"Cluster batch group prefixes");
        output=scratch(size_t(outputCount)*4,BufferUsage::Index|BufferUsage::CopySource,"Cluster batch visible indices");
        args=scratch(entries.size()*48,BufferUsage::Indirect|BufferUsage::CopySource,"Cluster batch indexed arguments");
        bytes=allNodes.size()*48+allClusters.size()*48+indices.size()*4+size_t(clusterCount)*16+size_t(groupCount)*8+size_t(outputCount)*4+entries.size()*48;
        sampler=resources.sampler({Filter::Nearest,AddressMode::ClampToEdge});
        auto empty=resources.texture({1,1,Format::R32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Cluster fail-open depth"});float one=1;
        d->writeTextureFloat(empty,&one,4);emptyDepth=resources.view(empty);
        for(size_t i=0;i<4;++i){auto path=directory+"/cluster-batch-"+std::array<const char*,4>{"mark","scan","scatter","late"}[i]+".comp";
            ComputePipelineDesc desc;desc.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};
            auto reflection=nlohmann::json::parse(readShaderText(desc.shader.reflectionPath));desc.threads=reflection.at("entryPoints").at(0).at("workgroup_size").get<std::array<uint32_t,3>>();desc.bindings={{0,{}},{1,{}}};desc.label="Batched cluster selection";
            for(const auto& u:reflection.at("ubos"))desc.bindings[u.at("set").get<size_t>()].entries.push_back({u.at("binding"),BindingType::UniformBuffer,ShaderStage::Compute,u.at("name"),u.at("block_size")});
            for(const auto& b:reflection.at("ssbos"))desc.bindings[b.at("set").get<size_t>()].entries.push_back({b.at("binding"),b.value("readonly",false)?BindingType::StorageRead:b.value("writeonly",false)?BindingType::StorageWrite:BindingType::StorageReadWrite,ShaderStage::Compute,b.at("name"),b.at("block_size")});
            if(reflection.contains("textures"))for(const auto& t:reflection.at("textures"))desc.bindings[t.at("set").get<size_t>()].entries.push_back({t.at("binding"),BindingType::SampledTexture,ShaderStage::Compute,t.at("name"),0});
            kernels[i]={resources.computePipeline(desc),desc.bindings};
        }
    }
};
bool GpuClusterCuller::recordBatched(Resources& frame,rhi::CommandList& commands,const FrameData& f,const std::vector<DrawPacket>& packets,std::vector<CulledMeshDraw>& draws,rhi::TextureViewHandle depth,uint32_t levels,uint32_t mode,const glm::mat4& historyVP){
    using namespace rhi;
    const auto limits=resources_.device->computeLimits();if(limits.maxStorageBindings<8||limits.maxSampledTextures<1)return false;
    auto mapPackets=[&]{std::vector<size_t> mapping(packets.size(),SIZE_MAX);if(!batch_)return mapping;
        std::vector<bool> used(batch_->entries.size());
        for(size_t i=0;i<packets.size();++i)if(compatible(packets[i],f))for(size_t j=0;j<batch_->entries.size();++j){const auto& e=batch_->entries[j];const auto& p=packets[i];
            if(!used[j]&&e.id==p.id&&e.geometry==p.clusters&&e.mesh==p.mesh){mapping[i]=j;used[j]=true;break;}}
        return mapping;
    };
    auto mapping=mapPackets();bool rebuild=!batch_;
    for(size_t i=0;i<packets.size();++i)if(compatible(packets[i],f)&&!packets[i].clusters->hierarchy->clusters.empty()&&mapping[i]==SIZE_MAX)rebuild=true;
    if((mode==1||mode==3)&&rebuild)return false; // Late/current occlusion must reuse the exact current-frame cut/storage.
    if(rebuild){try{batch_=std::make_shared<Batch>(resources_.device,directory_,packets,f);}catch(const ResourceBudgetExceeded&){batch_.reset();return false;}mapping=mapPackets();}
    auto& b=*batch_;b.active.clear();std::vector<BatchMesh> params(b.entries.size());
    for(size_t j=0;j<params.size();++j){params[j].ranges=b.entries[j].ranges;params[j].counts.y=b.entries[j].padded/64;}
    auto projection=f.viewProjection*glm::inverse(f.view);float focal=std::max(std::abs(projection[0][0])*f.viewportWidth,std::abs(projection[1][1])*f.viewportHeight)*.5f;
    size_t active=0;uint64_t sourceTriangles=0;
    for(size_t i=0;i<packets.size();++i)if(compatible(packets[i],f)){auto j=mapping[i];if(j==SIZE_MAX)continue;
        const auto& p=packets[i];const auto& h=*p.clusters->hierarchy;auto& v=params[j];
        bool lod=p.material->parameters().factors.w==0&&p.material->parameters().albedoAlpha.w>=1&&p.material->extension().settings.w==0&&p.material->extension().settings.z==0;
        v.vp=f.viewProjection*p.model;v.view=f.view*p.model;v.projection={focal,bound(p.model),f.nearPlane,f.clusters.pixelError};
        v.guard={1+4.f/f.viewportWidth,1+4.f/f.viewportHeight,std::abs(projection[2][3])<1e-5f?1.f:0.f,lod?1.f:0.f};
        v.counts={uint32_t(h.clusters.size()),b.entries[j].padded/64,h.sourceIndices,f.objectCulling?1u:0u};
        auto magnitude=glm::vec4(glm::max(glm::abs(p.mesh->boundsMin()),glm::abs(p.mesh->boundsMax())),1);glm::vec4 world(0),rounding(0);
        for(int k=0;k<4;++k)world+=glm::abs(p.model[k])*magnitude[k];for(int k=0;k<4;++k)rounding+=glm::abs(f.viewProjection[k])*world[k];v.rounding=rounding*4e-6f+glm::vec4(1e-7f);
        v.historyVP=historyVP*p.model;glm::vec4 oldRounding(0);for(int k=0;k<4;++k)oldRounding+=glm::abs(historyVP[k])*world[k];v.historyRounding=oldRounding*4e-6f+glm::vec4(1e-7f);
        draws[i]={b.output,b.args,j*48+(mode==3?24:0)};b.active.emplace_back(i,j);++active;sourceTriangles+=h.sourceIndices/3;
    }
    if(!active)return false; // Cached storage alone does not make this frame cluster-compatible.
    auto meshParams=frame.buffer({params.size()*sizeof(BatchMesh),BufferUsage::Storage,"Cluster batch current views"},params.data());
    const std::array<glm::uvec4,2> options={glm::uvec4(uint32_t(params.size()),f.viewportWidth,f.viewportHeight,depth?levels:0),glm::uvec4(mode,f.clusters.conservativeIntervals?1u:0u,std::min(b.clusterCount,limits.maxGroups[0]),b.clusterCount)};
    auto uniform=frame.buffer({32,BufferUsage::Uniform,"Cluster batch counts"},options.data());
    const bool fusedLate=mode==3&&f.clusters.fusedLate;
    for(size_t step=0;step<(fusedLate?2u:3u);++step){size_t stage=fusedLate&&step==1?3:step;const auto& kernel=b.kernels[stage];std::vector<BindingSetHandle> sets;
        for(const auto& layout:kernel.layouts){std::vector<BindingEntry> entries;
            for(const auto& e:layout.entries){if(e.type==BindingType::UniformBuffer){entries.push_back({e.binding,uniform,0,32,{},{}});continue;}
                if(e.type==BindingType::SampledTexture){entries.push_back({e.binding,{},0,0,depth?depth:b.emptyDepth,b.sampler});continue;}
                BufferHandle buffer;size_t size=0;switch(e.binding){
                case 0:buffer=b.nodes;size=b.nodeBytes;break;
                case 1:buffer=b.clusters;size=size_t(b.clusterCount)*48;break;
                case 2:buffer=b.source;size=b.sourceBytes;break;
                case 3:buffer=b.offsets;size=size_t(b.clusterCount)*16;break;
                case 4:buffer=b.groups;size=size_t(b.groupCount)*8;break;
                case 5:buffer=b.output;size=size_t(b.outputCount)*4;break;
                case 6:buffer=b.args;size=params.size()*48;break;
                case 7:buffer=meshParams;size=params.size()*288;break;
                default:throw std::logic_error("Unknown cluster batch binding");}
                entries.push_back({e.binding,buffer,0,size,{},{}});
            }sets.push_back(frame.bindings({layout,entries}));
        }
        commands.setLabel(std::string(mode==2?"clusters/early-":mode==3?"clusters/late-":depth?"clusters/occlusion-":"clusters/batch-")+std::array<const char*,4>{"mark","scan","scatter","fused"}[stage]);
        const uint32_t scatterX=std::min(b.clusterCount,limits.maxGroups[0]);
        commands.dispatch(kernel.pipeline,sets,{uint32_t(stage==0?b.groupCount:(stage==1||stage==3)?params.size():scatterX),stage==2?(b.clusterCount+scatterX-1)/scatterX:1u,1});
    }
    stats_.packets=active;stats_.sourceTriangles=sourceTriangles;stats_.bufferBytes=b.bytes;stats_.dispatches+=fusedLate?2:3;stats_.batched=true;stats_.occlusion=bool(depth)||mode==2||mode==3;stats_.twoPhase=mode==2||mode==3;if(mode==2)stats_.historyUsed=bool(depth);return true;
}
std::vector<CulledMeshDraw> GpuClusterCuller::recordOcclusion(Resources& frame,rhi::CommandList& commands,const FrameData& f,const std::vector<DrawPacket>& packets,std::vector<CulledMeshDraw> draws,rhi::TextureViewHandle depth,uint32_t levels){
    if(stats_.batched&&depth)recordBatched(frame,commands,f,packets,draws,depth,levels,1);return draws;
}
std::vector<ClusterCullCount> GpuClusterCuller::readBatchCounts(){std::vector<ClusterCullCount> out;if(!batch_)return out;
    std::vector<uint32_t> args(batch_->entries.size()*12);resources_.device->readBuffer(batch_->args,0,args.size()*4,args.data());
    for(auto item:batch_->active)out.push_back({item.first,batch_->entries[item.second].geometry->hierarchy->sourceIndices/3,args[item.second*12]/3,args[item.second*12+5]});return out;
}
std::vector<uint32_t> GpuClusterCuller::readBatchIndices(size_t packet){if(!batch_)return {};for(auto item:batch_->active)if(item.first==packet){uint32_t args[6];resources_.device->readBuffer(batch_->args,item.second*48,24,args);
    if(args[0]>batch_->entries[item.second].geometry->hierarchy->sourceIndices)throw std::runtime_error("Cluster batch argument overflow");
    std::vector<uint32_t> out(args[0]);if(args[0])resources_.device->readBuffer(batch_->output,size_t(args[2])*4,size_t(args[0])*4,out.data());return out;}return {};
}
}

namespace render {
std::vector<CulledMeshDraw> GpuClusterCuller::recordLate(Resources& frame,rhi::CommandList& commands,const FrameData& f,const std::vector<DrawPacket>& packets,rhi::TextureViewHandle depth,uint32_t levels){
    std::vector<CulledMeshDraw> late(packets.size());if(stats_.twoPhase&&stats_.historyUsed&&depth)recordBatched(frame,commands,f,packets,late,depth,levels,3);return late;
}
std::vector<ClusterVisibility> GpuClusterCuller::readVisibility(){
    std::vector<ClusterVisibility> out;if(!committed_||!stats_.batched||!batch_)return out;
    std::vector<glm::uvec4> flags(batch_->clusterCount);resources_.device->readBuffer(batch_->offsets,0,flags.size()*16,flags.data());
    for(auto item:batch_->active){const auto& e=batch_->entries[item.second];const auto& clusters=e.geometry->hierarchy->clusters;
        for(size_t i=0;i<clusters.size();++i){const auto v=flags[e.ranges.x+i];out.push_back({item.first,uint32_t(i),clusters[i].range.y,v.z!=0,v.w!=0,stats_.twoPhase&&v.w==0&&v.y!=0});}}
    return out;
}
}
