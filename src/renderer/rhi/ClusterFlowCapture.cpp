#include "renderer/rhi/ClusterFlowCapture.h"
#include "renderer/rhi/ShadowRenderer.h"
#include "renderer/rhi/FeatureScenes.h"
#include "renderer/RenderScene.h"
#include "utils/Camera.h"
#include "rhi/MemorySafety.h"
#include <stb/stb_image_write.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <set>
#include <map>
#include <iostream>
#include <cmath>
namespace render {
namespace {
namespace fs=std::filesystem;
void png(const fs::path& path,uint32_t w,uint32_t h,const std::vector<uint8_t>& pixels){if(!stbi_write_png(path.string().c_str(),w,h,4,pixels.data(),w*4))throw std::runtime_error("Cluster flow image write failed");}
glm::vec3 color(uint32_t key){key^=key>>16;key*=0x7feb352du;key^=key>>15;key*=0x846ca68bu;key^=key>>16;return glm::normalize(glm::vec3(.3f+.7f*float(key&255)/255,.3f+.7f*float((key>>8)&255)/255,.3f+.7f*float((key>>16)&255)/255));}
const std::array<glm::vec3,12> levelColors={glm::vec3(.15,.7,1),glm::vec3(.1,.95,.75),glm::vec3(.5,.95,.2),glm::vec3(1,.8,.15),glm::vec3(1,.45,.1),glm::vec3(1,.2,.4),glm::vec3(.85,.25,1),glm::vec3(.45,.3,1),glm::vec3(.7,.65,1),glm::vec3(1,.7,.8),glm::vec3(.7,1,.8),glm::vec3(1)};
bool protectedLeaf(const GpuMaterial& m){return m.parameters().factors.w!=0||m.parameters().albedoAlpha.w<1||m.extension().settings.w!=0||m.extension().settings.z!=0;}
std::vector<uint32_t> rasterKeyBases(const std::vector<DrawPacket>& packets){std::vector<uint32_t> bases;uint64_t next=1;
    for(const auto& p:packets){bases.push_back(uint32_t(next));next+=p.clusters?p.clusters->hierarchy->clusters.size():1;}
    if(next>16777216)throw std::runtime_error("Diagnostic raster keys exceed exact R32Float integers");return bases;
}
struct ObservedMemory {
    uint64_t processPeak=0,nativePeak=0;bool processSupported=false,nativeSupported=false;
    void sample(const std::shared_ptr<rhi::GraphicsDevice>& device){
        const auto process=rhi::memorySafety().stats().process;const auto native=device->nativeMemoryStats();
        processSupported|=process.footprintSupported;nativeSupported|=native.supported;
        processPeak=std::max(processPeak,process.footprintBytes);nativePeak=std::max(nativePeak,native.usedBytes);
    }
    nlohmann::json report()const{const auto safety=rhi::memorySafety().stats();return {
        {"process_budget_bytes",safety.processBudgetBytes},{"gpu_budget_bytes",safety.gpuBudgetBytes},
        {"sampled_process_peak_bytes",processPeak},{"sampled_native_gpu_peak_bytes",nativePeak},
        {"process_supported",processSupported},{"native_gpu_supported",nativeSupported},
        {"peak_pending_scratch_bytes",safety.peakReservedBytes},{"rejections",safety.rejections},
        {"scope","Boundary samples during hierarchy publication and capture, not an OS hard limit or a continuous lifetime peak"}};}
};
struct MembershipRaster {std::vector<float> depth,keys;uint64_t triangles=0,batches=0,maxBatchTriangles=0,batchTriangleLimit=0,scratchBytes=0;};
bool intersectsView(const glm::mat4& matrix,glm::vec3 low,glm::vec3 high){
    std::array<float,6> maximum;maximum.fill(-INFINITY);
    for(uint32_t i=0;i<8;++i){auto p=matrix*glm::vec4((i&1)?high.x:low.x,(i&2)?high.y:low.y,(i&4)?high.z:low.z,1);
        const std::array<float,6> planes={p.x+p.w,p.w-p.x,p.y+p.w,p.w-p.y,p.z,p.w-p.z};
        for(size_t plane=0;plane<planes.size();++plane)maximum[plane]=std::max(maximum[plane],planes[plane]);}
    return std::all_of(maximum.begin(),maximum.end(),[](float value){return value>=0;});
}
// Explain a visible patch using its actual selected index range. Connectivity
// is measured through exact position edges, independently of attribute seams.
nlohmann::json inspectSurfaceCluster(const ClusterHierarchy& h,uint32_t cluster,const MeshPayload& source,const glm::mat4& model){
    const auto& range=h.clusters.at(cluster).range;const uint32_t count=range.y/3;
    std::vector<uint32_t> parents(count);std::iota(parents.begin(),parents.end(),0);
    auto root=[&](uint32_t a){while(parents[a]!=a)a=parents[a];return a;};
    using Position=std::array<float,3>;using Edge=std::pair<Position,Position>;std::map<Edge,uint32_t> edges;
    auto geometry=nlohmann::json::array();double area=0,maxArea=0;
    for(uint32_t t=0;t<count;++t){std::array<glm::vec3,3> positions;std::array<Position,3> keys;auto triangle=nlohmann::json::array();
        for(uint32_t k=0;k<3;++k){auto local=source.vertices.at(h.indices.at(range.x+t*3+k)).position;keys[k]={local.x,local.y,local.z};positions[k]=glm::vec3(model*glm::vec4(local,1));triangle.push_back({positions[k].x,positions[k].y,positions[k].z});}
        geometry.push_back(triangle);auto triangleArea=.5*glm::length(glm::cross(positions[1]-positions[0],positions[2]-positions[0]));area+=triangleArea;maxArea=std::max(maxArea,double(triangleArea));
        for(uint32_t k=0;k<3;++k){auto a=keys[k],b=keys[(k+1)%3];if(b<a)std::swap(a,b);auto insertion=edges.emplace(Edge{a,b},t);if(!insertion.second)parents[root(t)]=root(insertion.first->second);}
    }
    std::map<uint32_t,uint32_t> components;for(uint32_t t=0;t<count;++t)++components[root(t)];std::vector<uint32_t> sizes;for(auto c:components)sizes.push_back(c.second);std::sort(sizes.rbegin(),sizes.rend());
    return {{"triangles",count},{"position_edge_components",sizes.size()},{"component_triangle_counts",sizes},{"largest_triangle_area_fraction",maxArea/std::max(area,1e-20)},{"world_triangles",geometry}};
}
// Replay the real GPU-selected cut in bounded batches. Keep the production
// vertex shader, original positions/UVs and alpha tests for exact depth parity.
// Each batch completes and releases native buffers before the next is created.
MembershipRaster membershipImage(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir,const fs::path& file,
    const RenderWorldSnapshot& snapshot,const std::vector<DrawPacket>& packets,const FrameData& f,const std::vector<ClusterVisibility>& flags,bool levels,ObservedMemory& memory){
    using namespace rhi;Resources resources(device);const auto w=f.viewportWidth,h=f.viewportHeight;
    // Covers CPU vertices/indices, GPU payloads and upload staging. A small
    // budget also reduces the batch instead of increasing the admission limit.
    const uint64_t scratchCap=std::min(uint64_t(64)*1024*1024,memorySafety().stats().processBudgetBytes/4);
    const uint32_t batchTriangles=uint32_t(std::min(uint64_t(65536),scratchCap/384));
    if(!batchTriangles)throw ResourceBudgetExceeded("No memory budget for cluster diagnostic batch");
    const auto scratchBytes=MemorySafety::bytes(batchTriangles,384);
    auto target=resources.texture({w,h,Format::RGBA8UNorm,TextureUsage::ColorAttachment|TextureUsage::CopySource,"Actual selected cluster colors"});
    auto keys=resources.texture({w,h,Format::R32Float,TextureUsage::ColorAttachment|TextureUsage::CopySource,"Unique selected cluster raster keys"});
    auto depth=resources.texture({w,h,Format::Depth32Float,TextureUsage::DepthAttachment|TextureUsage::CopySource,"Cluster diagnostic depth"});
    auto asset=[&](const char* name){auto p=dir+"/"+name;return ShaderAsset{p+".glsl",p+".metallib",p+".spv",p+".json","main0"};};
    GraphicsPipelineDesc desc;desc.vertex=asset("forward-material.vert");desc.fragment=asset("cluster-flow.frag");desc.vertexStride=sizeof(MeshVertex);
    auto geometry=ShadowRenderer::objectLayout();geometry.entries.push_back({6,BindingType::StorageRead,ShaderStage::Vertex,"MeshTangents",32});geometry.entries.push_back({2,BindingType::StorageRead,ShaderStage::Fragment,"FlowColors",0});
    desc.attributes=GpuMesh::attributes();desc.bindings={geometry,GpuMaterial::shadowLayout()};desc.depthAttachment=desc.depthTest=desc.depthWrite=true;desc.cull=CullMode::None;
    desc.additionalColorFormats={Format::R32Float};auto pipeline=resources.pipeline(desc);
    auto targetView=resources.view(target),depthView=resources.view(depth),keyView=resources.view(keys);
    const auto bases=rasterKeyBases(packets);auto last=packets.size()-1;size_t keyCount=bases[last]+(packets[last].clusters?packets[last].clusters->hierarchy->clusters.size():1);
    auto paletteScratch=memorySafety().reserve(MemorySafety::bytes(keyCount,16),"cluster diagnostic palette");
    std::vector<glm::vec4> palette(keyCount,glm::vec4(.5f,.5f,.5f,1));
    for(size_t packet=0;packet<packets.size();++packet)if(packets[packet].clusters){const auto& hierarchy=*packets[packet].clusters->hierarchy;
        for(size_t c=0;c<hierarchy.clusters.size();++c){auto level=hierarchy.nodes[hierarchy.clusters[c].range.z].links.w;auto key=bases[packet]+uint32_t(c);palette[key]=glm::vec4(levels?glm::normalize(levelColors.at(std::min(level,11u))):color(key),1);}}
    auto colors=resources.buffer({palette.size()*16,BufferUsage::Storage,"Unique cluster diagnostic palette"},palette.data());
    auto camera=resources.buffer({64,BufferUsage::Uniform,"Flow camera"},&f.viewProjection);
    auto wireGeometry=geometry;wireGeometry.entries.erase(std::remove_if(wireGeometry.entries.begin(),wireGeometry.entries.end(),[](const auto& e){return e.binding==2;}),wireGeometry.entries.end());
    const std::array<glm::vec4,2> emptyTangents={glm::vec4(0),glm::vec4(0)};auto tangents=resources.buffer({32,BufferUsage::Storage,"Flow tangent fallback"},emptyTangents.data());
    MembershipRaster raster;raster.scratchBytes=scratchBytes;raster.batchTriangleLimit=batchTriangles;
    // Group flags once; scanning the entire cut for every draw is unnecessary.
    std::vector<std::vector<const ClusterVisibility*>> selected(packets.size());
    for(const auto& flag:flags)if(flag.early||flag.late)selected.at(flag.packet).push_back(&flag);
    auto replay=[&](bool wire,PipelineHandle replayPipeline){
        auto scratch=memorySafety().reserve(scratchBytes,"bounded cluster diagnostic replay");
        std::vector<MeshVertex> vertices;std::vector<uint32_t> indices;vertices.reserve(batchTriangles*3);indices.reserve(batchTriangles*3);
        uint64_t batches=0;
        for(size_t packet=0;packet<packets.size();++packet){const auto& draw=packets[packet];
            if(draw.material->transparent()||!intersectsView(f.viewProjection*draw.model,draw.mesh->boundsMin(),draw.mesh->boundsMax()))continue;
            const auto& source=*snapshot.draws.at(packet).mesh;const auto inverseNormal=glm::inverse(glm::mat3(glm::transpose(glm::inverse(draw.model))));
            Resources objectResources(device);const std::array<glm::mat4,2> transforms={draw.model,glm::transpose(glm::inverse(draw.model))};
            auto object=objectResources.buffer({128,BufferUsage::Uniform,"Flow object"},transforms.data());
            auto bindings=wire?objectResources.bindings({wireGeometry,{{0,camera,0,64,{},{}},{1,object,0,128,{},{}},{6,tangents,0,32,{},{}}}}):
                objectResources.bindings({geometry,{{0,camera,0,64,{},{}},{1,object,0,128,{},{}},{6,tangents,0,32,{},{}},{2,colors,0,palette.size()*16,{},{}}}});
            auto flush=[&](){if(vertices.empty())return;
                memorySafety().check(0,"cluster diagnostic batch boundary");
                {
                    GpuMesh mesh(device,vertices,indices);auto commands=device->createCommandList();RenderPassDesc pass;pass.color=targetView;pass.depth=depthView;
                    pass.colorLoad=pass.depthLoad=(wire||batches)?LoadOp::Load:LoadOp::Clear;pass.clearColor={.035f,.045f,.065f,1};
                    if(!wire)pass.additionalColors={{keyView,batches?LoadOp::Load:LoadOp::Clear,StoreOp::Store,{0,0,0,0}}};
                    commands.beginRenderPass(pass);commands.bindPipeline(replayPipeline);commands.bindBindingSet(bindings);draw.material->bindShadow(commands);mesh.draw(commands);
                    commands.endRenderPass();device->submit(commands);device->waitIdle();memory.sample(device);
                }
                // Resources destroyed after completion, never retained as a
                // complete-scene replay for the later wire overlay.
                if(!wire){raster.triangles+=vertices.size()/3;raster.maxBatchTriangles=std::max(raster.maxBatchTriangles,uint64_t(vertices.size()/3));}
                ++batches;vertices.clear();indices.clear();
            };
            auto append=[&](uint32_t index,uint32_t key){auto vertex=source.vertices.at(index);
                vertex.normal=inverseNormal*glm::vec3(float(key&65535u)/65535.f,float(key>>16)/65535.f,1);
                indices.push_back(uint32_t(vertices.size()));vertices.push_back(vertex);if(vertices.size()==batchTriangles*3)flush();};
            if(!draw.clusters){for(auto index:source.indices)append(index,bases[packet]);}
            else{const auto& hierarchy=*draw.clusters->hierarchy;
                for(const auto* flag:selected[packet]){const auto& cluster=hierarchy.clusters.at(flag->cluster);
                    if(!intersectsView(f.viewProjection*draw.model,glm::vec3(cluster.low),glm::vec3(cluster.high)))continue;
                    for(uint32_t i=0;i<cluster.range.y;++i)append(hierarchy.indices.at(cluster.range.x+i),bases[packet]+flag->cluster);
                }}flush();
        }
        if(!batches&&!wire){auto commands=device->createCommandList();RenderPassDesc pass;pass.color=targetView;pass.depth=depthView;pass.clearColor={.035f,.045f,.065f,1};
            pass.additionalColors={{keyView,LoadOp::Clear,StoreOp::Store,{0,0,0,0}}};commands.beginRenderPass(pass);commands.endRenderPass();device->submit(commands);device->waitIdle();}
        if(!wire)raster.batches=batches;
    };
    replay(false,pipeline);png(file,w,h,device->readTexture(target));raster.depth=device->readTextureFloat(depth);raster.keys=device->readTextureFloat(keys);memory.sample(device);
    std::set<uint32_t> allowed{0};for(size_t p=0;p<packets.size();++p)if(!packets[p].clusters)allowed.insert(bases[p]);for(const auto& flag:flags)if(flag.early||flag.late)allowed.insert(bases[flag.packet]+flag.cluster);
    for(size_t i=0;i<raster.keys.size();++i)if(!std::isfinite(raster.keys[i])||raster.keys[i]!=std::floor(raster.keys[i])||!allowed.count(uint32_t(raster.keys[i]))||(raster.keys[i]==0)!=(raster.depth[i]==1))throw std::runtime_error("Diagnostic raster key disagrees with GPU membership / depth");
    if(!levels){auto raw=file.parent_path()/(file.stem().string()+"-ids.f32");std::ofstream out(raw,std::ios::binary);out.write(reinterpret_cast<const char*>(raster.keys.data()),raster.keys.size()*4);
        if(device->supportsWireframe()){
            auto wire=desc;wire.fragment=asset("cluster-flow-wire.frag");wire.bindings[0]=wireGeometry;wire.additionalColorFormats.clear();wire.wireframe=true;wire.depthWrite=false;wire.depthCompare=DepthCompare::LessEqual;
            replay(true,resources.pipeline(wire));png(file.parent_path()/(file.stem().string()+"-wire.png"),w,h,device->readTexture(target));memory.sample(device);
        }
    }
    return raster;
}
}
nlohmann::json captureClusterFlow(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir,const std::string& output,const RenderWorldSnapshot& snapshot,const SceneFrame& resolved,const std::vector<ClusterFlowView>& views){
    if(snapshot.draws.size()!=resolved.packets.size()||std::any_of(resolved.packets.begin(),resolved.packets.end(),[](const auto& p){return bool(p.mesh->instances());}))throw std::runtime_error("Cluster flow requires unbatched static source packets");
    fs::path root=fs::path(output)/"flow";fs::create_directories(root);auto f=resolved.frame;f.taa=f.ssao=f.rsm=false;f.ddgi.enabled=false;f.clusters.enabled=true;f.clusters.occlusion=false;f.hizCulling=false;
    ObservedMemory memory;memory.sample(device);
    ForwardPbrRenderer renderer(device,dir,f.viewportWidth,f.viewportHeight,PbrPath::Scene);nlohmann::json report={{"frames",nlohmann::json::array()},{"meshes",nlohmann::json::array()},{"level_colors",nlohmann::json::array()},{"scope","GPU-selected membership, original source vertices; debug colors are a separate native raster pass, no UI"}};
    report["viewport"]={f.viewportWidth,f.viewportHeight};report["object_culling"]=f.objectCulling;report["occlusion"]=false;
    report["diagnostic_raster"]="Actual selected geometry with original UVs and material alpha cutoff; non-cluster meshes gray; blended transparent surfaces omitted from depth/debug pass";
    const auto keyBases=rasterKeyBases(resolved.packets);report["raster_key_bases"]=keyBases;
    report["raster_key_convention"]="0 = clear; cluster key = packet prefix + local cluster index, collision-free across material meshes; small mesh key = its packet prefix";
    uint64_t contextTriangles=0;for(const auto& p:resolved.packets)if(!p.clusters)contextTriangles+=p.mesh->indexCount()/3;report["non_cluster_triangles"]=contextTriangles;
    for(const auto& c:levelColors){auto tint=glm::normalize(c);report["level_colors"].push_back({tint.r,tint.g,tint.b});}
    glm::vec3 center=f.cameraPosition;float distance=0; // Focus on the largest cluster-capable mesh.
    size_t largest=0;for(size_t i=0;i<resolved.packets.size();++i)if(resolved.packets[i].clusters&&resolved.packets[i].clusters->hierarchy->sourceIndices>largest){
        const auto& p=resolved.packets[i];largest=p.clusters->hierarchy->sourceIndices;center=glm::vec3(p.model*glm::vec4((p.mesh->boundsMin()+p.mesh->boundsMax())*.5f,1));
    }
    distance=glm::length(f.cameraPosition-center);const auto originalEye=f.cameraPosition;const auto projection=f.viewProjection*glm::inverse(f.view);
    report["vertical_fov_degrees"]=glm::degrees(2*std::atan(1.f/std::abs(projection[1][1])));report["near_plane"]=f.nearPlane;report["far_plane"]=f.farPlane;
    for(size_t i=0;i<resolved.packets.size();++i)if(resolved.packets[i].clusters){const auto& h=*resolved.packets[i].clusters->hierarchy;
        nlohmann::json levels=nlohmann::json::object();for(const auto& c:h.clusters){auto level=std::to_string(h.nodes[c.range.z].links.w);if(!levels.contains(level))levels[level]={{"clusters",0},{"triangles",0}};levels[level]["clusters"]=levels[level]["clusters"].get<uint64_t>()+1;levels[level]["triangles"]=levels[level]["triangles"].get<uint64_t>()+c.range.y/3;}
        report["meshes"].push_back({{"packet",i},{"source_mesh_id",snapshot.draws.at(i).mesh->id},{"source_triangles",h.sourceIndices/3},{"all_clusters",h.clusters.size()},{"leaf_clusters",h.leafClusters},{"nodes",h.nodes.size()},{"roots",h.roots.size()},{"budget_limited",h.budgetLimited},{"build_seconds",h.buildSeconds},{"resident_bytes",h.indices.size()*4+h.clusters.size()*48+h.nodes.size()*48},{"protected_leaf",protectedLeaf(*resolved.packets[i].material)},{"parent_attempts",h.parentAttempts},{"parent_accepted",h.parentAccepted},{"reduction_rejected",h.reductionRejected},{"levels",levels}});
    }
    // Default model-centered dolly, or explicit poses for a full-scene route.
    const std::array<float,6> scales={1.f,1.f,.45f,1.f,2.f,4.f};
    for(size_t sample=0;sample<(views.empty()?scales.size():views.size());++sample){auto current=f;current.clusters.pixelError=sample==0?0:4;
        if(!views.empty()){const auto& view=views[sample];current.clusters.pixelError=view.pixelError;current.cameraPosition=view.eye;current.view=glm::lookAt(view.eye,view.target,glm::vec3(0,1,0));current.viewProjection=projection*current.view;}
        else if(sample>1){current.cameraPosition=center+(originalEye-center)*scales[sample];current.view=glm::lookAt(current.cameraPosition,center,glm::vec3(0,1,0));current.viewProjection=projection*current.view;}
        const auto name=std::to_string(sample);rhi::memorySafety().check(0,"cluster flow view boundary");
        auto detail=current;detail.clusters.pixelError=0;renderer.render(detail,resolved.packets,resolved.exposure);uint64_t detailTriangles=0;for(auto count:renderer.readClusterCullCounts())detailTriangles+=count.selectedTriangles;
        png(root/(name+"-reference.png"),f.viewportWidth,f.viewportHeight,renderer.readOutput());
        auto detailHdr=renderer.readHDR();memory.sample(device);
        renderer.render(current,resolved.packets,resolved.exposure);auto flags=renderer.readClusterVisibility();memory.sample(device);
        bool cpuCutExact=!current.objectCulling;
        if(cpuCutExact)for(size_t i=0;i<resolved.packets.size();++i)if(resolved.packets[i].clusters){const auto& p=resolved.packets[i];const auto& h=*p.clusters->hierarchy;float one=0,inf=0;
            for(int c=0;c<3;++c){float a=0,b=0;for(int r=0;r<3;++r){a+=std::abs(p.model[c][r]);b+=std::abs(p.model[r][c]);}one=std::max(one,a);inf=std::max(inf,b);}
            auto projection=current.viewProjection*glm::inverse(current.view);float focal=std::max(std::abs(projection[0][0])*current.viewportWidth,std::abs(projection[1][1])*current.viewportHeight)*.5f;
            auto selected=selectClusterNodes(h,current.view*p.model,focal,std::sqrt(one*inf),current.nearPlane,current.clusters.pixelError,std::abs(projection[2][3])<1e-5f,!protectedLeaf(*p.material));std::set<uint32_t> nodes(selected.begin(),selected.end());std::vector<uint32_t> expected;
            for(const auto& c:h.clusters)if(nodes.count(c.range.z))expected.insert(expected.end(),h.indices.begin()+c.range.x,h.indices.begin()+c.range.x+c.range.y);
            if(renderer.readClusterIndices(i)!=expected)throw std::runtime_error("Dense scan GPU cut differs from independent CPU selection");
        }
        if(!renderer.clusterCullStats().batched||flags.empty())throw std::runtime_error("Cluster flow capture fell back to source geometry");
        png(root/(name+"-beauty.png"),f.viewportWidth,f.viewportHeight,renderer.readOutput());
        auto diagnostic=membershipImage(device,dir,root/(name+"-clusters.png"),snapshot,resolved.packets,current,flags,false,memory);
        auto levelDiagnostic=membershipImage(device,dir,root/(name+"-levels.png"),snapshot,resolved.packets,current,flags,true,memory);
        const auto& debugDepth=diagnostic.depth;const auto& levelDepth=levelDiagnostic.depth;
        auto depth=renderer.readDepth();if(depth.size()!=debugDepth.size()||debugDepth!=levelDepth)throw std::runtime_error("Cluster ID / level depth mismatch");
        if(diagnostic.keys!=levelDiagnostic.keys)throw std::runtime_error("Cluster ID / level raster keys mismatch");
        double maxDepthError=0;size_t coverageMismatch=0,depthOutlierCount=0;auto depthOutliers=nlohmann::json::array();
        for(size_t i=0;i<depth.size();++i){auto error=double(std::abs(depth[i]-debugDepth[i]));maxDepthError=std::max(maxDepthError,error);coverageMismatch+=(depth[i]<1)!=(debugDepth[i]<1);
            if(error>1e-5){++depthOutlierCount;if(depthOutliers.size()<128)depthOutliers.push_back({{"x",i%f.viewportWidth},{"y",i/f.viewportWidth},{"pbr",depth[i]},{"diagnostic",debugDepth[i]}});}}
        if(!depthOutliers.empty()){std::ofstream raw(root/(name+"-depth.bin"),std::ios::binary);raw.write(reinterpret_cast<const char*>(depth.data()),depth.size()*4);raw.write(reinterpret_cast<const char*>(debugDepth.data()),debugDepth.size()*4);}
        // Capture each view's own zero-error reference; camera changes are not LOD savings.
        auto hdr=renderer.readHDR();double squaredError=0,energy=0;for(size_t i=0;i<hdr.size();++i)if(i%4!=3){squaredError+=std::pow(double(hdr[i]-detailHdr[i]),2);energy+=std::pow(double(detailHdr[i]),2);}
        nlohmann::json selected=nlohmann::json::array(),levels=nlohmann::json::object();uint64_t triangles=0,clusters=0;
        // Large cuts are streamed one member per line; retaining all views as
        // JSON objects can cost more memory than the source geometry itself.
        const bool externalMembership=flags.size()>10000;const auto membershipName=name+"-membership.jsonl";
        std::ofstream members;if(externalMembership){members.open(root/membershipName);if(!members)throw std::runtime_error("Cluster membership write failed");}
        std::set<std::pair<uint32_t,uint32_t>> uniqueSelection;
        for(const auto& flag:flags)if(flag.early||flag.late){
            if(!uniqueSelection.emplace(flag.packet,flag.cluster).second)throw std::runtime_error("Duplicate GPU-selected cluster");
            const auto& hierarchy=*resolved.packets[flag.packet].clusters->hierarchy;auto level=hierarchy.nodes[hierarchy.clusters[flag.cluster].range.z].links.w;auto key=std::to_string(level);
            if(!levels.contains(key))levels[key]=0;levels[key]=levels[key].get<uint64_t>()+flag.indexCount/3;triangles+=flag.indexCount/3;++clusters;
            nlohmann::json member={{"packet",flag.packet},{"cluster",flag.cluster},{"raster_key",keyBases[flag.packet]+flag.cluster},{"node",hierarchy.clusters[flag.cluster].range.z},{"level",level},{"triangles",flag.indexCount/3}};
            if(externalMembership)members<<member.dump()<<'\n';else selected.push_back(std::move(member));
        }
        if(externalMembership){members.flush();if(!members)throw std::runtime_error("Cluster membership write failed");}
        uint64_t submitted=0;for(const auto& count:renderer.readClusterCullCounts())submitted+=count.selectedTriangles;
        if(submitted!=triangles)throw std::runtime_error("Cluster membership differs from GPU indirect draw counts");
        report["frames"].push_back({{"name",name},{"label",views.empty()?"":views[sample].label},{"distance_scale",views.empty()?nlohmann::json(scales[sample]):nlohmann::json(nullptr)},{"camera_distance",views.empty()?distance*scales[sample]:glm::length(current.cameraPosition-views[sample].target)},{"camera_position",{current.cameraPosition.x,current.cameraPosition.y,current.cameraPosition.z}},{"pixel_error",current.clusters.pixelError},{"cpu_cut_indices_exact",cpuCutExact?nlohmann::json(true):nlohmann::json(nullptr)},{"triangles",triangles},{"same_view_zero_error_triangles",detailTriangles},{"lod_reduction_percent",100.0*(1.0-double(triangles)/std::max(double(detailTriangles),1.0))},{"clusters",clusters},{"triangles_by_level",levels},{"zero_error_hdr_relative_rmse_percent",100*std::sqrt(squaredError/std::max(energy,1e-20))},{"debug_depth_max_error",maxDepthError},{"debug_coverage_mismatched_pixels",coverageMismatch},{"debug_depth_outlier_count",depthOutlierCount},{"debug_depth_outliers",depthOutliers},{"membership",std::move(selected)}});
        auto& frame=report["frames"].back();if(externalMembership)frame["membership_file"]=membershipName;
        frame["diagnostic_replay"]={{"batch_triangle_limit",diagnostic.batchTriangleLimit},{"max_batch_triangles",diagnostic.maxBatchTriangles},{"scratch_reservation_bytes",diagnostic.scratchBytes},{"filled_batches",diagnostic.batches},{"replayed_triangles",diagnostic.triangles},{"scope","Conservative view-bound rejection only; GPU-selected membership is unchanged"}};
        if(!views.empty()){const auto target=views[sample].target;frame["camera_target"]={target.x,target.y,target.z};}
        auto visibleKeys=std::set<uint32_t>();for(auto key:diagnostic.keys)if(key!=0)visibleKeys.insert(uint32_t(key));
        auto visibleClusters=nlohmann::json::array();for(const auto& flag:flags)if((flag.early||flag.late)&&visibleKeys.count(keyBases[flag.packet]+flag.cluster))visibleClusters.push_back(keyBases[flag.packet]+flag.cluster);
        frame["visible_cluster_raster_keys"]=std::move(visibleClusters);frame["diagnostic_raster_keys_exact"]=true;
        if(!views.empty()&&sample==1){auto probes=nlohmann::json::array();
            for(const auto& point:std::array<glm::vec2,3>{{{.22f,.43f},{.11f,.60f},{.76f,.34f}}}){
                uint32_t x=uint32_t(point.x*f.viewportWidth),y=uint32_t(point.y*f.viewportHeight),key=uint32_t(diagnostic.keys[y*f.viewportWidth+x]);
                auto member=std::find_if(flags.begin(),flags.end(),[&](const auto& flag){return (flag.early||flag.late)&&keyBases[flag.packet]+flag.cluster==key;});if(member==flags.end())continue;
                size_t packet=member->packet;uint32_t cluster=member->cluster;const auto& hierarchy=*resolved.packets[packet].clusters->hierarchy;
                auto shape=inspectSurfaceCluster(hierarchy,cluster,*snapshot.draws.at(packet).mesh,resolved.packets[packet].model);
                shape["raster_key"]=key;shape["packet"]=packet;shape["cluster"]=cluster;shape["level"]=hierarchy.nodes[hierarchy.clusters[cluster].range.z].links.w;shape["pixel"]={x,y};probes.push_back(shape);
            }report["surface_inspection"]=probes;
        }
        memory.sample(device);frame["memory_so_far"]=memory.report();
        std::cout<<"Cluster flow "<<sample<<": "<<triangles<<" triangles / "<<clusters<<" clusters, debug depth error "<<maxDepthError<<", coverage mismatch "<<coverageMismatch
            <<"; sampled process/GPU "<<memory.processPeak/1048576<<"/"<<memory.nativePeak/1048576<<" MiB, diagnostic batches "<<diagnostic.batches<<std::endl;
    }
    report["memory"]=memory.report();report["reference_renderer_reused"]=true;
    std::ofstream out(root/"flow.json");out<<report.dump(2)<<'\n';return report;
}
void captureClusterSceneFlow(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir,const std::string& name,const std::string& output){
    device->setMaxFramesInFlight(1);ObservedMemory preparation;preparation.sample(device);
    fs::create_directories(output);auto scene=makeClassicScene(name);scene->mainCamera()->setAspect(16.f/9);preparation.sample(device);
    SceneSnapshotBuilder builder;auto snapshot=builder.capture(scene,8,1280,720);auto runtime=*snapshot;
    runtime.rasterFrustumCulling=runtime.rasterInstanceBatching=false;
    runtime.frame.clusters.enabled=true;runtime.frame.objectCulling=false;
    SceneAdapter adapter(device);auto resolved=adapter.resolve(runtime);size_t expected=0;
    for(const auto& draw:runtime.draws)if(draw.mesh&&!draw.subdivision&&!draw.wireframe&&draw.pathTracingKind==0&&draw.mesh->indices.size()/3>=128)++expected;
    auto started=std::chrono::steady_clock::now(),lastUpdate=started;
    while(std::count_if(resolved.packets.begin(),resolved.packets.end(),[](const auto& p){return bool(p.clusters);})<expected){
        auto now=std::chrono::steady_clock::now();
        if(now-started>std::chrono::seconds(180))throw std::runtime_error("Cluster flow hierarchy publication timeout");
        if(now-lastUpdate>std::chrono::seconds(10)){std::cout<<"Waiting for "<<expected<<" mesh hierarchies"<<std::endl;lastUpdate=now;}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));resolved=adapter.resolve(runtime);preparation.sample(device);
    }
    std::vector<ClusterFlowView> views;
    if(name=="bistro"){
        // Dolly along the recorded street view, rather than towards the bounds
        // center of one material mesh spanning the entire city block.
        const auto eye=resolved.frame.cameraPosition;
        const auto forward=-glm::normalize(glm::vec3(glm::inverse(resolved.frame.view)[2]));
        const auto target=eye+forward*12.f;
        // The street turns behind the source camera. Raise distant views above
        // the storefronts instead of backing the camera through their walls.
        auto far=target+(eye-target)*1.6f,farther=target+(eye-target)*2.2f;far.y=22;farther.y=34;
        views={{"Zero-error / street",eye,target,0},{"4 px / same street camera",eye,target,4},
            {"Near / street",target+(eye-target)*.55f,target,4},
            {"Middle / street",eye,target,4},
            {"Far / elevated",far,target,4},
            {"Farther / elevated",farther,target,4},
            {"Block overview",{-42,48,-65},{0,8,0},4}};
    }
    if(name=="cluster-cave"||name=="cluster-cave-small"){
        std::ifstream input(name=="cluster-cave"?"samples/assets/benchmarks/procedural-cave/scene.json":"samples/assets/benchmarks/procedural-cave-small/scene.json");nlohmann::json layout;input>>layout;
        auto vector=[](const auto& a){return glm::vec3(a.at(0).template get<float>(),a.at(1).template get<float>(),a.at(2).template get<float>());};
        for(const auto& view:layout.at("views"))views.push_back({view.at("label").get<std::string>(),vector(view.at("eye")),vector(view.at("target")),view.at("pixel_error").get<float>()});
    }
    const double waitSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    auto report=captureClusterFlow(device,dir,output,runtime,resolved,views);
    preparation.sample(device);
    auto& observed=report["memory"];observed["sampled_process_peak_bytes"]=std::max(observed["sampled_process_peak_bytes"].get<uint64_t>(),preparation.processPeak);observed["sampled_native_gpu_peak_bytes"]=std::max(observed["sampled_native_gpu_peak_bytes"].get<uint64_t>(),preparation.nativePeak);
    report["scene"]=name;report["hierarchies_ready"]=expected;report["source_meshes"]=runtime.draws.size();
    uint64_t triangles=0;for(const auto& draw:runtime.draws)triangles+=draw.mesh->indices.size()/3;
    report["total_source_triangles"]=triangles;
    uint64_t uniqueSource=0,uniqueResident=0;std::set<uint64_t> uniqueMeshes;
    for(const auto& mesh:report["meshes"])if(uniqueMeshes.insert(mesh.at("source_mesh_id").get<uint64_t>()).second){uniqueSource+=mesh.at("source_triangles").get<uint64_t>();uniqueResident+=mesh.at("resident_bytes").get<uint64_t>();}
    report["unique_hierarchies"]=uniqueMeshes.size();report["unique_source_triangles"]=uniqueSource;report["unique_hierarchy_resident_bytes"]=uniqueResident;
    uint64_t clusterSource=0;for(const auto& m:report["meshes"])clusterSource+=m["source_triangles"].get<uint64_t>();
    report["degenerate_cluster_source_triangles"]=triangles-clusterSource-report["non_cluster_triangles"].get<uint64_t>();
    report["publication_wait_seconds"]=waitSeconds;
    if(name=="bistro"||name=="cluster-cave"||name=="cluster-cave-small")for(const auto& frame:report["frames"]){
        if(frame["debug_coverage_mismatched_pixels"].get<uint64_t>()!=0||frame["debug_depth_max_error"].get<double>()>1e-5)throw std::runtime_error("Scene cluster diagnostic depth differs from selected PBR geometry");
    }
    std::ofstream out(fs::path(output)/"flow/flow.json");out<<report.dump(2)<<'\n';scene->destroy();
    std::cout<<"Cluster scene flow captured: "<<name<<" ("<<expected<<" hierarchies ready)"<<std::endl;
}
void captureDenseClusterScene(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir,const std::string& name,const std::string& output){
    fs::create_directories(output);auto scene=makeClassicScene(name);scene->mainCamera()->setAspect(16.f/9);SceneSnapshotBuilder builder;auto snapshot=builder.capture(scene,8,960,540);
    auto runtime=*snapshot;runtime.frame.clusters.enabled=true;runtime.frame.objectCulling=false;SceneAdapter adapter(device);auto resolved=adapter.resolve(runtime);
    size_t expected=0;for(const auto& draw:runtime.draws)if(draw.mesh&&draw.mesh->indices.size()/3>=128)++expected;
    auto started=std::chrono::steady_clock::now();while(std::count_if(resolved.packets.begin(),resolved.packets.end(),[](const auto& p){return bool(p.clusters);})<expected){
        if(std::chrono::steady_clock::now()-started>std::chrono::seconds(180))throw std::runtime_error("Dense cluster hierarchy publication timeout");std::this_thread::sleep_for(std::chrono::milliseconds(5));resolved=adapter.resolve(runtime);
    }
    resolved.frame.taa=resolved.frame.ssao=resolved.frame.rsm=false;resolved.frame.ddgi.enabled=false;resolved.frame.clusters.pixelError=4;
    auto flow=captureClusterFlow(device,dir,output,*snapshot,resolved);nlohmann::json report={{"scene",name},{"flow",flow},{"benchmark",nlohmann::json::object()}};
    // Single scanned model, plus a separately identified three-copy overlap stress.
    for(bool layered:{false,true}){auto packets=resolved.packets;if(layered){size_t largest=0,index=0;for(size_t i=0;i<packets.size();++i)if(packets[i].clusters&&packets[i].mesh->indexCount()>largest){largest=packets[i].mesh->indexCount();index=i;}
            auto front=glm::normalize(glm::vec3(glm::inverse(resolved.frame.view)[2]))*-1.f;
            for(int i=1;i<=2;++i){auto p=packets[index];p.id+=100000+i;p.model=glm::translate(glm::mat4(1),front*(float(i)*1.5f)+glm::vec3(.15f*i,0,0))*p.model;packets.push_back(p);}}
        auto& bench=report["benchmark"][layered?"three_scan_overlap":"single_scan"];bench["scope"]="960x540, 4px LOD, Metal timestamp pass sum and continuous cull-to-geometry window; 3 frame-interleaved rotated rounds x (8 warmup + 24 measured); reference outside timing";
        ForwardPbrRenderer reference(device,dir,960,540,PbrPath::Scene);auto base=resolved.frame;base.clusters.occlusion=false;base.hizCulling=false;
        reference.render(base,packets,resolved.exposure);auto referenceHDR=reference.readHDR(),referenceDepth=reference.readDepth();auto referencePixels=reference.readOutput();
        std::array<std::vector<double>,3> sums,windows,cpus;std::array<nlohmann::json,3> raw;for(auto& r:raw)r=nlohmann::json::array();
        const std::array<const char*,3> names={"no_occlusion","two_stage_original","two_stage_fused"};
        std::array<std::unique_ptr<ForwardPbrRenderer>,3> views;
        for(auto& view:views)view=std::make_unique<ForwardPbrRenderer>(device,dir,960,540,PbrPath::Scene);
        for(size_t round=0;round<3;++round)for(int sample=0;sample<32;++sample)for(size_t order=0;order<3;++order){size_t mode=(round+order+sample)%3;auto& activeRenderer=*views[mode];auto f=base;f.clusters.occlusion=mode!=0;f.clusters.twoPhase=true;f.clusters.conservativeIntervals=false;f.clusters.fusedPyramid=f.clusters.fusedLate=mode==2;
            device->drainGpuProfile();auto start=std::chrono::steady_clock::now();activeRenderer.render(f,packets,resolved.exposure);double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();auto hdr=activeRenderer.readHDR();auto spans=device->drainGpuProfile();if(sample<8)continue;
                double sum=0,first=1e30,last=0;for(const auto& t:spans)if(t.label.rfind("geometry",0)==0||t.label.rfind("hiz/",0)==0||t.label.rfind("clusters/",0)==0){sum+=t.endMilliseconds-t.startMilliseconds;first=std::min(first,t.startMilliseconds);last=std::max(last,t.endMilliseconds);raw[mode].push_back({{"round",round},{"sample",sample-8},{"stage",t.label},{"start_ms",t.startMilliseconds},{"end_ms",t.endMilliseconds}});}
                sums[mode].push_back(sum);windows[mode].push_back(last-first);cpus[mode].push_back(cpu);
        }
        for(size_t mode=0;mode<3;++mode){auto& activeRenderer=*views[mode];
            if(!activeRenderer.clusterCullStats().batched||(mode!=0&&!activeRenderer.clusterCullStats().twoPhase))throw std::runtime_error("Dense benchmark fell back from batched cluster occlusion");
            auto hdr=activeRenderer.readHDR();double error=0;for(size_t i=0;i<hdr.size();++i)error=std::max(error,double(std::abs(hdr[i]-referenceHDR[i])));bool exact=activeRenderer.readOutput()==referencePixels&&activeRenderer.readDepth()==referenceDepth;if(error>.001||!exact)throw std::runtime_error("Dense scan occlusion changed visible pixels");
            uint64_t cut=0,early=0,late=0;for(auto c:activeRenderer.readClusterVisibility()){if(c.selected)cut+=c.indexCount/3;if(c.early)early+=c.indexCount/3;if(c.late)late+=c.indexCount/3;}
            bench[names[mode]]={{"batched",activeRenderer.clusterCullStats().batched},{"two_phase",activeRenderer.clusterCullStats().twoPhase},{"source_triangles",activeRenderer.clusterCullStats().sourceTriangles},{"cut_triangles",cut},{"early_triangles",early},{"late_triangles",late},{"hdr_max_error",error},{"depth_display_exact",exact},{"dispatches",activeRenderer.clusterCullStats().dispatches},{"buffer_bytes",activeRenderer.clusterCullStats().bufferBytes}};
            if(mode==2)png(fs::path(output)/(layered?"overlap-final.png":"single-final.png"),960,540,activeRenderer.readOutput());
        }
        auto median=[](std::vector<double> v){std::sort(v.begin(),v.end());return (v[(v.size()-1)/2]+v[v.size()/2])*.5;};
        for(size_t mode=0;mode<3;++mode){auto& r=bench[names[mode]];r["gpu_pass_sum_ms"]=median(sums[mode]);r["gpu_window_ms"]=median(windows[mode]);r["cpu_render_ms"]=median(cpus[mode]);r["samples"]=sums[mode].size();r["raw"]=raw[mode];}
    }
    scene->destroy();std::ofstream out(fs::path(output)/"dense.json");out<<report.dump(2)<<'\n';std::cout<<"Dense cluster scene captured: "<<name<<'\n';
}
}
