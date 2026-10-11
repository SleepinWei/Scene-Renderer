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
double maxError(const std::vector<float>& a,const std::vector<float>& b){if(a.size()!=b.size())throw std::runtime_error("Cluster attachment size");double error=0;for(size_t i=0;i<a.size();++i){if(!std::isfinite(a[i])||!std::isfinite(b[i]))throw std::runtime_error("Nonfinite cluster attachment");error=std::max(error,double(std::abs(a[i]-b[i])));}return error;}
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
// Replay the GPU's early-then-late draw order independently on the CPU. This
// distinguishes missing surfaces from depth ties when recovered faces move
// behind other packets in the submission order.
std::vector<DrawPacket> orderedVisibility(std::shared_ptr<rhi::GraphicsDevice> device,const std::vector<DrawPacket>& packets,const std::vector<ClusterVisibility>& visible){
    std::vector<DrawPacket> out;
    for(bool late:{false,true})for(size_t p=0;p<packets.size();++p){
        if(!packets[p].clusters){if(!late)out.push_back(packets[p]);continue;}
        const auto& h=*packets[p].clusters->hierarchy;auto forced=std::make_shared<ClusterHierarchy>();forced->nodes.resize(1);forced->nodes[0].links.w=0;forced->roots={0};
        for(const auto& v:visible)if(v.packet==p&&(late?v.late:v.early)){
            auto c=h.clusters.at(v.cluster);c.range.x=uint32_t(forced->indices.size());c.range.z=0;
            const auto& original=h.clusters.at(v.cluster);forced->indices.insert(forced->indices.end(),h.indices.begin()+original.range.x,h.indices.begin()+original.range.x+original.range.y);forced->clusters.push_back(c);
        }
        if(forced->indices.empty())continue;forced->sourceIndices=uint32_t(forced->indices.size());forced->leafClusters=uint32_t(forced->clusters.size());
        auto draw=packets[p];draw.clusters=std::make_shared<GpuClusterGeometry>(device,forced);out.push_back(draw);
    }
    return out;
}
json hierarchyStats(const std::vector<DrawPacket>& packets){
    uint64_t source=0,leaves=0,all=0,nodes=0,roots=0,bytes=0,positions=0,attributes=0,seams=0;double build=0;size_t limited=0;
    json meshes=json::array();std::set<const ClusterHierarchy*> unique;
    uint64_t uniqueBytes=0,uniqueSource=0;double uniqueBuild=0;
    for(size_t p=0;p<packets.size();++p)if(packets[p].clusters){const auto& h=*packets[p].clusters->hierarchy;source+=h.sourceIndices/3;leaves+=h.leafClusters;all+=h.clusters.size();nodes+=h.nodes.size();roots+=h.roots.size();
        auto resident=h.indices.size()*4+h.clusters.size()*48+h.nodes.size()*48;bytes+=resident;build+=h.buildSeconds;limited+=h.budgetLimited;positions+=h.positionVertices;attributes+=h.attributeVertices;seams+=h.seamVertices;
        if(unique.insert(&h).second){uniqueBytes+=resident;uniqueSource+=h.sourceIndices/3;uniqueBuild+=h.buildSeconds;}
        uint32_t small=0;for(const auto& c:h.clusters)if(h.nodes[c.range.z].links.y==UINT32_MAX&&c.range.y<64*3)++small;
        meshes.push_back({{"packet",p},{"source_triangles",h.sourceIndices/3},{"leaf_clusters",h.leafClusters},{"under_half_full_leaves",small},{"roots",h.roots.size()},{"all_clusters",h.clusters.size()},{"nodes",h.nodes.size()},{"resident_bytes",resident},{"build_seconds",h.buildSeconds},{"budget_limited",h.budgetLimited},{"position_vertices",h.positionVertices},{"attribute_vertices",h.attributeVertices},{"seam_vertices",h.seamVertices},{"normal_error_bound",h.normalErrorBound},
            {"routing_nodes",h.routingNodes},{"parent_attempts",h.parentAttempts},{"parent_accepted",h.parentAccepted},{"reduction_rejected",h.reductionRejected},{"normal_rejected_candidates",h.normalRejected},{"uv_rejected_candidates",h.uvRejected},{"position_rejected_candidates",h.positionRejected}});
    }
    return {{"source_triangles",source},{"leaf_clusters",leaves},{"all_clusters",all},{"nodes",nodes},{"roots",roots},{"resident_bytes",uniqueBytes},{"build_seconds_sum",uniqueBuild},{"instance_resident_bytes_sum",bytes},{"instance_build_seconds_sum",build},{"unique_hierarchies",unique.size()},{"unique_source_triangles",uniqueSource},{"budget_limited_meshes",limited},{"position_vertices",positions},{"attribute_vertices",attributes},{"seam_vertices",seams},{"leaf_utilization_percent",100.*source/std::max(double(leaves*128),1.)},{"meshes",meshes}};
}
}
void captureClusterBuildComparison(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir,const std::string& sceneName,const std::string& output){
    const bool parentCompare=std::getenv("SCENERENDERER_CLUSTER_PARENT_COMPARE")!=nullptr;
    const float normalCeiling=std::getenv("SCENERENDERER_CLUSTER_PARENT_NORMAL_ERROR")?std::stof(std::getenv("SCENERENDERER_CLUSTER_PARENT_NORMAL_ERROR")):ClusterBuildSettings{}.maxNormalError;
    namespace fs=std::filesystem;fs::path root(output);fs::create_directories(root);auto scene=makeClassicScene(sceneName);scene->mainCamera()->setAspect(16.f/9);SceneSnapshotBuilder builder;auto snapshot=builder.capture(scene,8,960,540);
    auto runtime=*snapshot;runtime.frame.clusters.enabled=true;SceneAdapter adapter(device);auto resolved=adapter.resolve(runtime);size_t expected=0;
    for(const auto& draw:runtime.draws)if(draw.mesh&&!draw.subdivision&&!draw.wireframe&&draw.pathTracingKind==0&&draw.mesh->indices.size()/3>=128)++expected;
    auto start=Clock::now();while(std::count_if(resolved.packets.begin(),resolved.packets.end(),[](const auto& p){return bool(p.clusters);})<expected){
        if(std::chrono::duration<double>(Clock::now()-start).count()>180)throw std::runtime_error("Cluster comparison async timeout");std::this_thread::sleep_for(std::chrono::milliseconds(5));resolved=adapter.resolve(runtime);}
    const double asyncPublicationSeconds=std::chrono::duration<double>(Clock::now()-start).count();auto optimized=resolved.packets,legacy=optimized;
    // Measure both CPU builders on this same thread, alternating order by mesh.
    // Async worker wall times are not comparable to a sequential legacy build.
    std::map<const MeshPayload*,std::pair<std::shared_ptr<GpuClusterGeometry>,std::shared_ptr<GpuClusterGeometry>>> built;
    for(size_t p=0;p<legacy.size();++p)if(legacy[p].clusters){const auto& source=*snapshot->draws.at(p).mesh;
        if(auto found=built.find(&source);found!=built.end()){legacy[p].clusters=found->second.first;optimized[p].clusters=found->second.second;continue;}
        for(size_t order=0;order<2;++order){bool spatial=(p+order)%2!=0;ClusterBuildSettings settings;settings.spatialPartition=parentCompare||spatial;
            settings.maxNormalError=normalCeiling;
            if(parentCompare&&!spatial){settings.parentGroupSize=2;settings.localizedErrors=false;settings.maxNormalError=.05f;}
            settings.maxError=std::max(1e-5f,glm::length(legacy[p].mesh->boundsMax()-legacy[p].mesh->boundsMin())*.02f);
            auto geometry=std::make_shared<GpuClusterGeometry>(device,buildClusterHierarchy(source,settings));(spatial?optimized[p]:legacy[p]).clusters=std::move(geometry);}
        built.emplace(&source,std::make_pair(legacy[p].clusters,optimized[p].clusters));}
    auto frame=resolved.frame;frame.taa=frame.ssao=frame.rsm=false;frame.ddgi.enabled=false;frame.clusters.enabled=true;frame.clusters.occlusion=false;frame.hizCulling=false;frame.clusters.pixelError=4;
    json report={{"scene",sceneName},{"scope","same build/scene/materials/camera; position adjacency and spatial fill vs original attribute BFS; no UI; profiler overhead included"},{"hierarchy",{{"legacy",hierarchyStats(legacy)},{"optimized",hierarchyStats(optimized)}}},{"quality",json::object()},{"benchmark",json::object()}};
    if(parentCompare)report["scope"]="same build/materials/camera; previous spatial pair parents and group-global bounds vs connected parent groups and localized conservative bounds";
    report["builder_names"]={{"legacy",parentCompare?"Spatial pairs / global error":"Attribute BFS"},{"optimized",parentCompare?"Connected groups / endpoint error":"Spatial partition / grouped parents"}};
    report["cpu_construction_scope"]="One build per unique mesh/builder on the same thread, alternating builder order by mesh; sum of construction wall times, not CPU cycles or application load latency.";
    report["optimized_build_settings"]={{"triangles_per_cluster",128},{"max_levels",12},{"position_error", "2% of each mesh bounds diagonal"},{"uv_error",.03},{"normal_error",normalCeiling},{"min_parent_reduction",.125},{"parent_group_size",ClusterBuildSettings{}.parentGroupSize},{"localized_error_bounds",true},{"byte_budget_per_mesh",64*1024*1024}};
    if(parentCompare)report["baseline_build_settings"]={{"spatial_partition",true},{"parent_group_size",2},{"localized_error_bounds",false},{"normal_error",.05},{"uv_error",.03},{"min_parent_reduction",.125}};
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
    if(parentCompare)for(bool old:{true,false}){
        const auto& packets=old?legacy:optimized;ForwardPbrRenderer moving(device,dir,960,540,PbrPath::Scene),unoccluded(device,dir,960,540,PbrPath::Scene);
        auto projection=frame.viewProjection*glm::inverse(frame.view);json trajectory=json::array();
        for(uint32_t pose=0;pose<5;++pose){auto current=frame;current.clusters.pixelError=4;current.clusters.occlusion=false;
            if(pose==1){current.cameraPosition={6,8,18};current.view=glm::lookAt(current.cameraPosition,glm::vec3(-8,6,-55),glm::vec3(0,1,0));}
            if(pose==2){current.cameraPosition={-6,7,-70};current.view=glm::lookAt(current.cameraPosition,glm::vec3(6,6,20),glm::vec3(0,1,0));}
            if(pose==3){current.cameraPosition={11,6,-20};current.view=glm::lookAt(current.cameraPosition,glm::vec3(-10,6,-85),glm::vec3(0,1,0));}
            current.viewProjection=projection*current.view;unoccluded.render(current,packets,resolved.exposure);
            auto hdr=unoccluded.readHDR(),depth=unoccluded.readDepth();auto pixels=unoccluded.readOutput();
            auto forced=current;forced.objectCulling=false;reference.render(forced,cpuCut(device,packets,current),resolved.exposure);
            double cpuHdr=maxError(hdr,reference.readHDR()),cpuDepth=maxError(depth,reference.readDepth());std::vector<double> cpuG;
            for(uint32_t attachment=0;attachment<6;++attachment)cpuG.push_back(maxError(unoccluded.readGBuffer(attachment),reference.readGBuffer(attachment)));
            if(cpuHdr>1e-6||cpuDepth>1e-6||pixels!=reference.readOutput()||*std::max_element(cpuG.begin(),cpuG.end())>1e-6)throw std::runtime_error("Moving parent cut differs from independent CPU render");
            current.clusters.occlusion=true;current.clusters.twoPhase=true;moving.render(current,packets,resolved.exposure);
            auto hizHdr=maxError(moving.readHDR(),hdr),hizDepth=maxError(moving.readDepth(),depth);std::vector<double> hizG;
            for(uint32_t attachment=0;attachment<6;++attachment)hizG.push_back(maxError(moving.readGBuffer(attachment),unoccluded.readGBuffer(attachment)));
            bool displayExact=moving.readOutput()==pixels;json orderReplay=nullptr;
            if(hizHdr>1e-6||hizDepth>1e-6||!displayExact||*std::max_element(hizG.begin(),hizG.end())>1e-6){
                auto replay=current;replay.clusters.occlusion=false;replay.clusters.pixelError=0;replay.objectCulling=false;
                reference.render(replay,orderedVisibility(device,packets,moving.readClusterVisibility()),resolved.exposure);
                double replayHdr=maxError(moving.readHDR(),reference.readHDR()),replayDepth=maxError(moving.readDepth(),reference.readDepth());std::vector<double> replayG;
                for(uint32_t attachment=0;attachment<6;++attachment)replayG.push_back(maxError(moving.readGBuffer(attachment),reference.readGBuffer(attachment)));
                bool replayExact=moving.readOutput()==reference.readOutput();
                orderReplay={{"hdr_max_error",replayHdr},{"depth_max_error",replayDepth},{"gbuffer_max_errors",replayG},{"display_exact",replayExact}};
                json failure={{"builder",old?"legacy":"optimized"},{"pose",pose},{"hdr_max_error",hizHdr},{"depth_max_error",hizDepth},{"gbuffer_max_errors",hizG},{"quality",quality(moving.readHDR(),hdr,moving.readOutput(),pixels)}};
                failure["ordered_cpu_replay"]=orderReplay;
                auto diagnostic="motion-order-"+std::string(old?"legacy":"optimized")+"-"+std::to_string(pose);
                std::ofstream(root/(diagnostic+".json"))<<failure.dump(2)<<'\n';png(root/(diagnostic+"-reference.png"),current,pixels);png(root/(diagnostic+"-hiz.png"),current,moving.readOutput());
                // Require identical visibility depth AND exact independent
                // replay. Changed frontmost depth remains a hard failure.
                if(hizDepth>0||replayHdr>1e-6||replayDepth>0||!replayExact||*std::max_element(replayG.begin(),replayG.end())>1e-6)
                    throw std::runtime_error("Moving parent HiZ changed visible geometry: "+failure.dump());}
            uint64_t early=0,late=0;for(auto v:moving.readClusterVisibility()){if(v.early)early+=v.indexCount/3;if(v.late)late+=v.indexCount/3;}
            trajectory.push_back({{"pose",pose},{"camera",{current.cameraPosition.x,current.cameraPosition.y,current.cameraPosition.z}},{"cpu_cut_hdr_max_error",cpuHdr},{"cpu_cut_depth_max_error",cpuDepth},{"cpu_cut_gbuffer_max_errors",cpuG},{"hiz_hdr_max_error",hizHdr},{"hiz_depth_max_error",hizDepth},{"hiz_gbuffer_max_errors",hizG},{"display_exact",displayExact},{"ordered_cpu_replay",orderReplay},{"hiz_quality",quality(moving.readHDR(),hdr,moving.readOutput(),pixels)},{"early_triangles",early},{"late_triangles",late},{"history_used",moving.clusterCullStats().historyUsed}});
            if(!old)png(root/("motion-"+std::to_string(pose)+".png"),current,pixels);
        }
        report["temporal_quality"][old?"legacy":"optimized"]=trajectory;
    }
    struct Mode {const char* name;bool old;float error;bool hiz;bool enabled=true;};const std::array<Mode,7> modes={Mode{"legacy_0px",true,0,false},{"optimized_0px",false,0,false},{"legacy_4px",true,4,false},{"optimized_4px",false,4,false},{"legacy_4px_hiz",true,4,true},{"optimized_4px_hiz",false,4,true},{"source",false,0,false,false}};
    std::array<std::unique_ptr<ForwardPbrRenderer>,7> views;for(auto& v:views)v=std::make_unique<ForwardPbrRenderer>(device,dir,960,540,PbrPath::Scene);
    std::array<std::vector<double>,7> windows,sums,cpus;std::array<json,7> raw;for(auto& r:raw)r=json::array();
    if(std::getenv("SCENERENDERER_CLUSTER_BENCHMARK")){
        for(size_t round=0;round<3;++round)for(int sample=0;sample<32;++sample)for(size_t order=0;order<modes.size();++order){size_t m=(round+sample+order)%modes.size();auto f=frame;f.clusters.enabled=modes[m].enabled;f.clusters.pixelError=modes[m].error;f.clusters.occlusion=modes[m].hiz;f.clusters.twoPhase=true;
            const auto& packets=modes[m].old?legacy:optimized;device->drainGpuProfile();auto start=Clock::now();views[m]->render(f,packets,resolved.exposure);double cpu=std::chrono::duration<double,std::milli>(Clock::now()-start).count();views[m]->readHDR();auto spans=device->drainGpuProfile();if(sample<8)continue;
            double first=1e30,last=0,sum=0;for(const auto& s:spans)if(s.label.rfind("geometry",0)==0||s.label.rfind("clusters/",0)==0||s.label.rfind("hiz/",0)==0){double ms=s.endMilliseconds-s.startMilliseconds;first=std::min(first,s.startMilliseconds);last=std::max(last,s.endMilliseconds);sum+=ms;
                raw[m].push_back({{"round",round},{"sample",sample-8},{"stage",s.label},{"kind",s.kind},{"start_ms",s.startMilliseconds},{"end_ms",s.endMilliseconds},{"ms",ms}});}
            if(last<=first)throw std::runtime_error("GPU counters unavailable for cluster comparison");windows[m].push_back(last-first);sums[m].push_back(sum);cpus[m].push_back(cpu);
        }
        auto median=[](std::vector<double> v){std::sort(v.begin(),v.end());return .5*(v[(v.size()-1)/2]+v[v.size()/2]);};
        for(size_t m=0;m<modes.size();++m){auto& v=*views[m];if(modes[m].enabled&&(!v.clusterCullStats().batched||(modes[m].hiz&&!v.clusterCullStats().twoPhase)))throw std::runtime_error("Cluster comparison silently fell back");uint64_t triangles=0;for(auto c:v.readClusterCullCounts())triangles+=c.selectedTriangles;
            if(!modes[m].enabled)for(const auto& p:optimized)if(p.mesh)triangles+=p.mesh->indexCount()/3;
            auto& result=report["benchmark"][modes[m].name];result={{"samples",windows[m].size()},{"gpu_window_median_ms",median(windows[m])},{"gpu_pass_sum_median_ms",median(sums[m])},{"cpu_render_median_ms",median(cpus[m])},{"triangles",triangles},{"buffer_bytes",v.clusterCullStats().bufferBytes},{"raw",raw[m]}};
            if(modes[m].hiz){uint64_t early=0,late=0,rejected=0;
                for(const auto& flag:v.readClusterVisibility()){if(flag.early)early+=flag.indexCount/3;if(flag.late)late+=flag.indexCount/3;if(flag.selected&&!flag.early&&!flag.late)rejected+=flag.indexCount/3;}
                result["visibility"]={{"early_triangles",early},{"late_triangles",late},{"rejected_selected_triangles",rejected},{"history_used",v.clusterCullStats().historyUsed}};
                if(early+late!=triangles)throw std::runtime_error("Cliff visibility does not match final indirect draw count");}
        }
    }
    auto oldResolved=resolved;oldResolved.packets=legacy;auto newResolved=resolved;newResolved.packets=optimized;
    captureClusterFlow(device,dir,(root/"legacy").string(),*snapshot,oldResolved);captureClusterFlow(device,dir,(root/"optimized").string(),*snapshot,newResolved);
    std::ofstream out(root/"comparison.json");out<<report.dump(2)<<'\n';scene->destroy();std::cout<<"Cluster build comparison captured: "<<sceneName<<'\n';
}
}
