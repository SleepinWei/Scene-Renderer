#include "renderer/rhi/ClusterFlowCapture.h"
#include "renderer/rhi/ShadowRenderer.h"
#include "renderer/rhi/FeatureScenes.h"
#include "renderer/RenderScene.h"
#include "utils/Camera.h"
#include <stb/stb_image_write.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <set>
#include <iostream>
namespace render {
namespace {
namespace fs=std::filesystem;
void png(const fs::path& path,uint32_t w,uint32_t h,const std::vector<uint8_t>& pixels){if(!stbi_write_png(path.string().c_str(),w,h,4,pixels.data(),w*4))throw std::runtime_error("Cluster flow image write failed");}
glm::vec3 color(uint32_t key){key=key*1664525u+1013904223u;return {.3f+.7f*float(key&255)/255,.3f+.7f*float((key>>8)&255)/255,.3f+.7f*float((key>>16)&255)/255};}
const std::array<glm::vec3,12> levelColors={glm::vec3(.15,.7,1),glm::vec3(.1,.95,.75),glm::vec3(.5,.95,.2),glm::vec3(1,.8,.15),glm::vec3(1,.45,.1),glm::vec3(1,.2,.4),glm::vec3(.85,.25,1),glm::vec3(.45,.3,1),glm::vec3(.7,.65,1),glm::vec3(1,.7,.8),glm::vec3(.7,1,.8),glm::vec3(1)};
// Diagnostic only: duplicate selected triangle vertices to attach stable cluster
// colors. Membership is read from the real GPU, never recomputed by the CPU.
void membershipImage(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir,const fs::path& file,
    const RenderWorldSnapshot& snapshot,const std::vector<DrawPacket>& packets,const FrameData& f,const std::vector<ClusterVisibility>& flags,bool levels){
    using namespace rhi;Resources resources(device);const auto w=f.viewportWidth,h=f.viewportHeight;
    auto target=resources.texture({w,h,Format::RGBA8UNorm,TextureUsage::ColorAttachment|TextureUsage::CopySource,"Actual selected cluster colors"});
    auto depth=resources.texture({w,h,Format::Depth32Float,TextureUsage::DepthAttachment,"Cluster diagnostic depth"});
    auto asset=[&](const char* name){auto p=dir+"/"+name;return ShaderAsset{p+".glsl",p+".metallib",p+".spv",p+".json","main0"};};
    GraphicsPipelineDesc desc;desc.vertex=asset("cluster-flow.vert");desc.fragment=asset("cluster-flow.frag");desc.vertexStride=sizeof(MeshVertex);
    desc.attributes={{0,VertexFormat::Float3,0},{1,VertexFormat::Float3,12}};desc.bindings={ShadowRenderer::objectLayout()};desc.depthAttachment=desc.depthTest=desc.depthWrite=true;desc.cull=CullMode::None;
    auto pipeline=resources.pipeline(desc);auto commands=device->createCommandList();RenderPassDesc pass;pass.color=resources.view(target);pass.depth=resources.view(depth);pass.clearColor={.035f,.045f,.065f,1};commands.beginRenderPass(pass);commands.bindPipeline(pipeline);
    auto camera=resources.buffer({64,BufferUsage::Uniform,"Flow camera"},&f.viewProjection);std::vector<std::shared_ptr<GpuMesh>> meshes;
    for(size_t packet=0;packet<packets.size();++packet){if(!packets[packet].clusters)continue;
        const auto& hierarchy=*packets[packet].clusters->hierarchy;const auto& source=snapshot.draws.at(packet).mesh->vertices;
        std::vector<MeshVertex> vertices;std::vector<uint32_t> indices;
        for(const auto& flag:flags)if(flag.packet==packet&&(flag.early||flag.late)){
            const auto& cluster=hierarchy.clusters.at(flag.cluster);auto level=hierarchy.nodes.at(cluster.range.z).links.w;
            auto tint=levels?levelColors.at(std::min(level,11u)):color(flag.cluster+uint32_t(packet)*997);
            for(uint32_t i=0;i<cluster.range.y;++i){auto vertex=source.at(hierarchy.indices.at(cluster.range.x+i));
                const auto normal=glm::normalize(glm::mat3(glm::transpose(glm::inverse(packets[packet].model)))*vertex.normal);
                vertex.normal=tint*(.55f+.45f*std::abs(glm::dot(normal,glm::normalize(glm::vec3(.4,1,.7)))));
                indices.push_back(uint32_t(vertices.size()));vertices.push_back(vertex);
            }
        }
        if(vertices.empty())continue;auto mesh=std::make_shared<GpuMesh>(device,vertices,indices);meshes.push_back(mesh);
        const std::array<glm::mat4,2> transforms={packets[packet].model,glm::mat4(1)};auto object=resources.buffer({128,BufferUsage::Uniform,"Flow object"},transforms.data());
        commands.bindBindingSet(resources.bindings({ShadowRenderer::objectLayout(),{{0,camera,0,64,{},{}},{1,object,0,128,{},{}}}}));mesh->draw(commands);
    }
    commands.endRenderPass();device->submit(commands);png(file,w,h,device->readTexture(target));
}
}
nlohmann::json captureClusterFlow(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir,const std::string& output,const RenderWorldSnapshot& snapshot,const SceneFrame& resolved){
    fs::path root=fs::path(output)/"flow";fs::create_directories(root);auto f=resolved.frame;f.taa=f.ssao=f.rsm=false;f.ddgi.enabled=false;f.clusters.enabled=true;f.clusters.occlusion=false;f.hizCulling=false;
    ForwardPbrRenderer renderer(device,dir,f.viewportWidth,f.viewportHeight,PbrPath::Scene),fullDetail(device,dir,f.viewportWidth,f.viewportHeight,PbrPath::Scene);nlohmann::json report={{"frames",nlohmann::json::array()},{"meshes",nlohmann::json::array()},{"level_colors",nlohmann::json::array()},{"scope","GPU-selected membership, original source vertices; debug colors are a separate native raster pass, no UI"}};
    for(const auto& c:levelColors)report["level_colors"].push_back({c.r,c.g,c.b});
    glm::vec3 center=f.cameraPosition;float distance=0; // Focus on the largest cluster-capable mesh.
    size_t largest=0;for(size_t i=0;i<resolved.packets.size();++i)if(resolved.packets[i].clusters&&resolved.packets[i].clusters->hierarchy->sourceIndices>largest){
        const auto& p=resolved.packets[i];largest=p.clusters->hierarchy->sourceIndices;center=glm::vec3(p.model*glm::vec4((p.mesh->boundsMin()+p.mesh->boundsMax())*.5f,1));
    }
    distance=glm::length(f.cameraPosition-center);const auto originalEye=f.cameraPosition;const auto projection=f.viewProjection*glm::inverse(f.view);
    for(size_t i=0;i<resolved.packets.size();++i)if(resolved.packets[i].clusters){const auto& h=*resolved.packets[i].clusters->hierarchy;
        nlohmann::json levels=nlohmann::json::object();for(const auto& c:h.clusters){auto level=std::to_string(h.nodes[c.range.z].links.w);if(!levels.contains(level))levels[level]={{"clusters",0},{"triangles",0}};levels[level]["clusters"]=levels[level]["clusters"].get<uint64_t>()+1;levels[level]["triangles"]=levels[level]["triangles"].get<uint64_t>()+c.range.y/3;}
        auto& material=*resolved.packets[i].material;bool protectedLeaf=material.parameters().factors.w!=0||material.parameters().albedoAlpha.w<1||material.extension().settings.w!=0||material.extension().settings.z!=0;
        report["meshes"].push_back({{"packet",i},{"source_triangles",h.sourceIndices/3},{"all_clusters",h.clusters.size()},{"leaf_clusters",h.leafClusters},{"nodes",h.nodes.size()},{"roots",h.roots.size()},{"budget_limited",h.budgetLimited},{"build_seconds",h.buildSeconds},{"resident_bytes",h.indices.size()*4+h.clusters.size()*48+h.nodes.size()*48},{"protected_leaf",protectedLeaf},{"levels",levels}});
    }
    // Same 4 px threshold throughout the dolly; only the camera distance changes.
    const std::array<float,6> scales={1.f,1.f,.45f,1.f,2.f,4.f};
    for(size_t sample=0;sample<scales.size();++sample){auto current=f;current.clusters.pixelError=sample==0?0:4;
        if(sample>1){current.cameraPosition=center+(originalEye-center)*scales[sample];current.view=glm::lookAt(current.cameraPosition,center,glm::vec3(0,1,0));current.viewProjection=projection*current.view;}
        auto detail=current;detail.clusters.pixelError=0;fullDetail.render(detail,resolved.packets,resolved.exposure);uint64_t detailTriangles=0;for(auto count:fullDetail.readClusterCullCounts())detailTriangles+=count.selectedTriangles;
        renderer.render(current,resolved.packets,resolved.exposure);auto flags=renderer.readClusterVisibility();
        bool cpuCutExact=!current.objectCulling;
        if(cpuCutExact)for(size_t i=0;i<resolved.packets.size();++i)if(resolved.packets[i].clusters){const auto& p=resolved.packets[i];const auto& h=*p.clusters->hierarchy;float one=0,inf=0;
            for(int c=0;c<3;++c){float a=0,b=0;for(int r=0;r<3;++r){a+=std::abs(p.model[c][r]);b+=std::abs(p.model[r][c]);}one=std::max(one,a);inf=std::max(inf,b);}
            auto projection=current.viewProjection*glm::inverse(current.view);float focal=std::max(std::abs(projection[0][0])*current.viewportWidth,std::abs(projection[1][1])*current.viewportHeight)*.5f;
            auto selected=selectClusterNodes(h,current.view*p.model,focal,std::sqrt(one*inf),current.nearPlane,current.clusters.pixelError);std::set<uint32_t> nodes(selected.begin(),selected.end());std::vector<uint32_t> expected;
            for(const auto& c:h.clusters)if(nodes.count(c.range.z))expected.insert(expected.end(),h.indices.begin()+c.range.x,h.indices.begin()+c.range.x+c.range.y);
            if(renderer.readClusterIndices(i)!=expected)throw std::runtime_error("Dense scan GPU cut differs from independent CPU selection");
        }
        if(!renderer.clusterCullStats().batched||flags.empty())throw std::runtime_error("Cluster flow capture fell back to source geometry");const auto name=std::to_string(sample);
        png(root/(name+"-beauty.png"),f.viewportWidth,f.viewportHeight,renderer.readOutput());
        membershipImage(device,dir,root/(name+"-clusters.png"),snapshot,resolved.packets,current,flags,false);
        membershipImage(device,dir,root/(name+"-levels.png"),snapshot,resolved.packets,current,flags,true);
        nlohmann::json selected=nlohmann::json::array(),levels=nlohmann::json::object();uint64_t triangles=0;
        for(auto flag:flags)if(flag.early||flag.late){const auto& h=*resolved.packets[flag.packet].clusters->hierarchy;auto level=h.nodes[h.clusters[flag.cluster].range.z].links.w;auto key=std::to_string(level);
            if(!levels.contains(key))levels[key]=0;levels[key]=levels[key].get<uint64_t>()+flag.indexCount/3;triangles+=flag.indexCount/3;
            selected.push_back({{"packet",flag.packet},{"cluster",flag.cluster},{"node",h.clusters[flag.cluster].range.z},{"level",level},{"triangles",flag.indexCount/3}});
        }
        report["frames"].push_back({{"name",name},{"distance_scale",scales[sample]},{"camera_distance",distance*scales[sample]},{"pixel_error",current.clusters.pixelError},{"cpu_cut_indices_exact",cpuCutExact?nlohmann::json(true):nlohmann::json(nullptr)},{"triangles",triangles},{"same_view_zero_error_triangles",detailTriangles},{"lod_reduction_percent",100.0*(1.0-double(triangles)/std::max(double(detailTriangles),1.0))},{"clusters",selected.size()},{"triangles_by_level",levels},{"membership",selected}});
    }
    std::ofstream out(root/"flow.json");out<<report.dump(2)<<'\n';return report;
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
