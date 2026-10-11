#include "renderer/rhi/ClusterFlowCapture.h"
#include "renderer/rhi/FeatureScenes.h"
#include "renderer/RenderScene.h"
#include "utils/Camera.h"
#include <stb/stb_image_write.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <thread>

namespace render {
namespace {
using json=nlohmann::json;
using Clock=std::chrono::steady_clock;
void png(const std::filesystem::path& p,const FrameData& f,const std::vector<uint8_t>& pixels){
    if(!stbi_write_png(p.string().c_str(),f.viewportWidth,f.viewportHeight,4,pixels.data(),f.viewportWidth*4))throw std::runtime_error("Cluster comparison PNG write");
}
json quality(const std::vector<float>& image,const std::vector<float>& reference,const std::vector<uint8_t>& pixels,const std::vector<uint8_t>& expected){
    double squares=0,energy=0,worst=0,displaySquares=0;size_t changed=0;int displayWorst=0;
    for(size_t i=0;i<image.size();++i)if(i%4!=3){double delta=double(image[i])-reference[i];if(!std::isfinite(image[i]))throw std::runtime_error("Nonfinite comparison HDR");squares+=delta*delta;energy+=double(reference[i])*reference[i];worst=std::max(worst,std::abs(delta));}
    for(size_t i=0;i<pixels.size();i+=4){bool different=false;for(int c=0;c<3;++c){int d=int(pixels[i+c])-expected[i+c];different|=d!=0;displayWorst=std::max(displayWorst,std::abs(d));displaySquares+=d*d;}changed+=different;}
    return {{"hdr_relative_rmse",std::sqrt(squares/std::max(energy,1e-30))},{"hdr_max_error",worst},{"changed_display_pixels",changed},{"display_rmse_255",std::sqrt(displaySquares/(pixels.size()/4*3))},{"display_max_error_255",displayWorst}};
}
double maxError(const std::vector<float>& a,const std::vector<float>& b){if(a.size()!=b.size())throw std::runtime_error("Cluster attachment size");double error=0;for(size_t i=0;i<a.size();++i)error=std::max(error,double(std::abs(a[i]-b[i])));return error;}
float scaleBound(const glm::mat4& m){float one=0,inf=0;for(int i=0;i<3;++i){float a=0,b=0;for(int j=0;j<3;++j){a+=std::abs(m[i][j]);b+=std::abs(m[j][i]);}one=std::max(one,a);inf=std::max(inf,b);}return std::sqrt(one*inf);}
// Independent CPU group cut, rendered as forced leaves against the original
// vertex/tangent/material and shadow data. Only the index representation changes.
std::vector<DrawPacket> cpuCut(std::shared_ptr<rhi::GraphicsDevice> device,const std::vector<DrawPacket>& packets,const FrameData& f){
    auto out=packets;auto projection=f.viewProjection*glm::inverse(f.view);float focal=.5f*std::max(std::abs(projection[0][0])*f.viewportWidth,std::abs(projection[1][1])*f.viewportHeight);
    for(size_t p=0;p<out.size();++p)if(out[p].clusters){const auto& h=*out[p].clusters->hierarchy;const auto& material=*out[p].material;
        bool allow=material.parameters().factors.w==0&&material.parameters().albedoAlpha.w>=1&&material.extension().settings.w==0&&material.extension().settings.z==0;
        auto selected=selectClusterNodes(h,f.view*out[p].model,focal,scaleBound(out[p].model),f.nearPlane,f.clusters.pixelError,std::abs(projection[2][3])<1e-5f,allow);std::set<uint32_t> nodes(selected.begin(),selected.end());
        auto reference=std::make_shared<ClusterHierarchy>();reference->nodes.resize(1);reference->nodes[0].links.w=0;reference->roots={0};
        for(const auto& c:h.clusters)if(nodes.count(c.range.z)){auto copy=c;copy.range.x=uint32_t(reference->indices.size());copy.range.z=0;
            reference->indices.insert(reference->indices.end(),h.indices.begin()+c.range.x,h.indices.begin()+c.range.x+c.range.y);reference->clusters.push_back(copy);}
        reference->sourceIndices=uint32_t(reference->indices.size());reference->leafClusters=uint32_t(reference->clusters.size());
        if(reference->indices.empty())throw std::runtime_error("CPU cut lost a mesh");out[p].clusters=std::make_shared<GpuClusterGeometry>(device,reference);
    }
    return out;
}
json hierarchyStats(const std::vector<DrawPacket>& packets){
    uint64_t source=0,leaves=0,all=0,nodes=0,roots=0,bytes=0,positions=0,attributes=0,seams=0;double build=0;size_t limited=0;
    json meshes=json::array();
    for(size_t p=0;p<packets.size();++p)if(packets[p].clusters){const auto& h=*packets[p].clusters->hierarchy;source+=h.sourceIndices/3;leaves+=h.leafClusters;all+=h.clusters.size();nodes+=h.nodes.size();roots+=h.roots.size();
        auto resident=h.indices.size()*4+h.clusters.size()*48+h.nodes.size()*48;bytes+=resident;build+=h.buildSeconds;limited+=h.budgetLimited;positions+=h.positionVertices;attributes+=h.attributeVertices;seams+=h.seamVertices;
        uint32_t small=0;for(const auto& c:h.clusters)if(h.nodes[c.range.z].links.y==UINT32_MAX&&c.range.y<64*3)++small;
        meshes.push_back({{"packet",p},{"source_triangles",h.sourceIndices/3},{"leaf_clusters",h.leafClusters},{"under_half_full_leaves",small},{"roots",h.roots.size()},{"all_clusters",h.clusters.size()},{"nodes",h.nodes.size()},{"resident_bytes",resident},{"build_seconds",h.buildSeconds},{"budget_limited",h.budgetLimited},{"position_vertices",h.positionVertices},{"attribute_vertices",h.attributeVertices},{"seam_vertices",h.seamVertices},{"normal_error_bound",h.normalErrorBound}});
    }
    return {{"source_triangles",source},{"leaf_clusters",leaves},{"all_clusters",all},{"nodes",nodes},{"roots",roots},{"resident_bytes",bytes},{"build_seconds_sum",build},{"budget_limited_meshes",limited},{"position_vertices",positions},{"attribute_vertices",attributes},{"seam_vertices",seams},{"leaf_utilization_percent",100.*source/std::max(double(leaves*128),1.)},{"meshes",meshes}};
}
}
void captureClusterBuildComparison(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir,const std::string& sceneName,const std::string& output){
    namespace fs=std::filesystem;fs::path root(output);fs::create_directories(root);auto scene=makeClassicScene(sceneName);scene->mainCamera()->setAspect(16.f/9);SceneSnapshotBuilder builder;auto snapshot=builder.capture(scene,8,960,540);
    auto runtime=*snapshot;runtime.frame.clusters.enabled=true;SceneAdapter adapter(device);auto resolved=adapter.resolve(runtime);size_t expected=0;
    for(const auto& draw:runtime.draws)if(draw.mesh&&!draw.subdivision&&!draw.wireframe&&draw.pathTracingKind==0&&draw.mesh->indices.size()/3>=128)++expected;
    auto start=Clock::now();while(std::count_if(resolved.packets.begin(),resolved.packets.end(),[](const auto& p){return bool(p.clusters);})<expected){
        if(std::chrono::duration<double>(Clock::now()-start).count()>180)throw std::runtime_error("Cluster comparison async timeout");std::this_thread::sleep_for(std::chrono::milliseconds(5));resolved=adapter.resolve(runtime);}
    const double asyncPublicationSeconds=std::chrono::duration<double>(Clock::now()-start).count();auto optimized=resolved.packets,legacy=optimized;
    // Measure both CPU builders on this same thread, alternating order by mesh.
    // Async worker wall times are not comparable to a sequential legacy build.
    for(size_t p=0;p<legacy.size();++p)if(legacy[p].clusters){const auto& source=*snapshot->draws.at(p).mesh;
        for(size_t order=0;order<2;++order){bool spatial=(p+order)%2!=0;ClusterBuildSettings settings;settings.spatialPartition=spatial;
            settings.maxError=std::max(1e-5f,glm::length(legacy[p].mesh->boundsMax()-legacy[p].mesh->boundsMin())*.02f);
            auto geometry=std::make_shared<GpuClusterGeometry>(device,buildClusterHierarchy(source,settings));(spatial?optimized[p]:legacy[p]).clusters=std::move(geometry);}}
    auto frame=resolved.frame;frame.taa=frame.ssao=frame.rsm=false;frame.ddgi.enabled=false;frame.clusters.enabled=true;frame.clusters.occlusion=false;frame.hizCulling=false;frame.clusters.pixelError=4;
    json report={{"scene",sceneName},{"scope","same build/scene/materials/camera; position adjacency and spatial fill vs original attribute BFS; no UI; profiler overhead included"},{"hierarchy",{{"legacy",hierarchyStats(legacy)},{"optimized",hierarchyStats(optimized)}}},{"quality",json::object()},{"benchmark",json::object()}};
    report["cpu_construction_scope"]="One build per mesh/builder on the same thread, alternating builder order by mesh; sum of construction wall times, not CPU cycles or application load latency.";
    report["optimized_build_settings"]={{"triangles_per_cluster",128},{"max_levels",12},{"position_error", "2% of each mesh bounds diagonal"},{"uv_error",.03},{"normal_error",.05},{"min_parent_reduction",.125},{"byte_budget_per_mesh",64*1024*1024}};
    report["optimized_async_publication_seconds"]=asyncPublicationSeconds;
    ForwardPbrRenderer original(device,dir,960,540,PbrPath::Scene),reference(device,dir,960,540,PbrPath::Scene),view(device,dir,960,540,PbrPath::Scene);
    auto full=frame;full.clusters.enabled=false;original.render(full,optimized,resolved.exposure);auto fullHDR=original.readHDR(),fullDepth=original.readDepth();auto fullPixels=original.readOutput();png(root/"source.png",frame,fullPixels);
    for(bool old:{true,false}){const auto& packets=old?legacy:optimized;std::string name=old?"legacy":"optimized";
        for(float budget:{0.f,4.f,16.f}){auto f=frame;f.clusters.pixelError=budget;view.render(f,packets,resolved.exposure);auto hdr=view.readHDR(),depth=view.readDepth();auto pixels=view.readOutput();
            auto independent=f;independent.objectCulling=false;reference.render(independent,cpuCut(device,packets,f),resolved.exposure);
            double referenceError=maxError(hdr,reference.readHDR()),depthError=maxError(depth,reference.readDepth());auto referencePixels=reference.readOutput();std::vector<double> gbufferErrors;
            for(uint32_t attachment=0;attachment<6;++attachment)gbufferErrors.push_back(maxError(view.readGBuffer(attachment),reference.readGBuffer(attachment)));
            if(referenceError>1e-6||depthError>1e-6||pixels!=referencePixels||*std::max_element(gbufferErrors.begin(),gbufferErrors.end())>1e-6)throw std::runtime_error("GPU render differs from independent CPU group cut");
            auto key=name+"_"+std::to_string(int(budget))+"px";auto result=quality(hdr,fullHDR,pixels,fullPixels);uint64_t triangles=0,clusters=0;for(auto c:view.readClusterCullCounts()){triangles+=c.selectedTriangles;clusters+=c.selectedClusters;}
            std::vector<double> sourceGbufferErrors;for(uint32_t attachment=0;attachment<6;++attachment)sourceGbufferErrors.push_back(maxError(view.readGBuffer(attachment),original.readGBuffer(attachment)));
            result["triangles"]=triangles;result["clusters"]=clusters;result["cpu_cut_hdr_max_error"]=referenceError;result["cpu_cut_depth_max_error"]=depthError;result["cpu_cut_display_exact"]=true;result["cpu_cut_gbuffer_max_errors"]=gbufferErrors;result["source_gbuffer_max_errors"]=sourceGbufferErrors;result["source_depth_max_error"]=maxError(depth,fullDepth);
            report["quality"][key]=result;png(root/(key+".png"),f,pixels);
            auto hz=f;hz.clusters.occlusion=true;hz.clusters.twoPhase=true;view.render(hz,packets,resolved.exposure);view.render(hz,packets,resolved.exposure);
            const double hizHdrError=maxError(view.readHDR(),hdr);std::vector<double> hizGbufferErrors;
            for(uint32_t attachment=0;attachment<6;++attachment)hizGbufferErrors.push_back(maxError(view.readGBuffer(attachment),reference.readGBuffer(attachment)));
            if(view.readOutput()!=pixels||view.readDepth()!=depth||hizHdrError>1e-6||*std::max_element(hizGbufferErrors.begin(),hizGbufferErrors.end())>1e-6)throw std::runtime_error("Rebuilt hierarchy HiZ changed visible surface");
            result["hiz_depth_display_exact"]=true;result["hiz_hdr_max_error"]=hizHdrError;result["hiz_gbuffer_max_errors"]=hizGbufferErrors;report["quality"][key]=result;
        }
    }
    struct Mode {const char* name;bool old;float error;bool hiz;};const std::array<Mode,6> modes={Mode{"legacy_0px",true,0,false},{"optimized_0px",false,0,false},{"legacy_4px",true,4,false},{"optimized_4px",false,4,false},{"legacy_4px_hiz",true,4,true},{"optimized_4px_hiz",false,4,true}};
    std::array<std::unique_ptr<ForwardPbrRenderer>,6> views;for(auto& v:views)v=std::make_unique<ForwardPbrRenderer>(device,dir,960,540,PbrPath::Scene);
    std::array<std::vector<double>,6> windows,sums,cpus;std::array<json,6> raw;for(auto& r:raw)r=json::array();
    if(std::getenv("SCENERENDERER_CLUSTER_BENCHMARK")){
        for(size_t round=0;round<3;++round)for(int sample=0;sample<32;++sample)for(size_t order=0;order<modes.size();++order){size_t m=(round+sample+order)%modes.size();auto f=frame;f.clusters.pixelError=modes[m].error;f.clusters.occlusion=modes[m].hiz;f.clusters.twoPhase=true;
            const auto& packets=modes[m].old?legacy:optimized;device->drainGpuProfile();auto start=Clock::now();views[m]->render(f,packets,resolved.exposure);double cpu=std::chrono::duration<double,std::milli>(Clock::now()-start).count();views[m]->readHDR();auto spans=device->drainGpuProfile();if(sample<8)continue;
            double first=1e30,last=0,sum=0;for(const auto& s:spans)if(s.label.rfind("geometry",0)==0||s.label.rfind("clusters/",0)==0||s.label.rfind("hiz/",0)==0){double ms=s.endMilliseconds-s.startMilliseconds;first=std::min(first,s.startMilliseconds);last=std::max(last,s.endMilliseconds);sum+=ms;
                raw[m].push_back({{"round",round},{"sample",sample-8},{"stage",s.label},{"kind",s.kind},{"start_ms",s.startMilliseconds},{"end_ms",s.endMilliseconds},{"ms",ms}});}
            if(last<=first)throw std::runtime_error("GPU counters unavailable for cluster comparison");windows[m].push_back(last-first);sums[m].push_back(sum);cpus[m].push_back(cpu);
        }
        auto median=[](std::vector<double> v){std::sort(v.begin(),v.end());return .5*(v[(v.size()-1)/2]+v[v.size()/2]);};
        for(size_t m=0;m<modes.size();++m){auto& v=*views[m];if(!v.clusterCullStats().batched||(modes[m].hiz&&!v.clusterCullStats().twoPhase))throw std::runtime_error("Cluster comparison silently fell back");uint64_t triangles=0;for(auto c:v.readClusterCullCounts())triangles+=c.selectedTriangles;
            report["benchmark"][modes[m].name]={{"samples",windows[m].size()},{"gpu_window_median_ms",median(windows[m])},{"gpu_pass_sum_median_ms",median(sums[m])},{"cpu_render_median_ms",median(cpus[m])},{"triangles",triangles},{"buffer_bytes",v.clusterCullStats().bufferBytes},{"raw",raw[m]}};}
    }
    auto oldResolved=resolved;oldResolved.packets=legacy;auto newResolved=resolved;newResolved.packets=optimized;
    captureClusterFlow(device,dir,(root/"legacy").string(),*snapshot,oldResolved);captureClusterFlow(device,dir,(root/"optimized").string(),*snapshot,newResolved);
    std::ofstream out(root/"comparison.json");out<<report.dump(2)<<'\n';scene->destroy();std::cout<<"Cluster build comparison captured: "<<sceneName<<'\n';
}
}
