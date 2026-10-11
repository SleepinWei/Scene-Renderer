#include "renderer/rhi/ClusterGeometry.h"
#include "renderer/rhi/ClusterFlowCapture.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/rhi/FeatureScenes.h"
#include "renderer/RenderScene.h"
#include "utils/Camera.h"
#include <json/json.hpp>
#include <stb/stb_image_write.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cmath>
#include <array>
#include <limits>
#include <map>
namespace render {
namespace {
void png(const std::filesystem::path& path,uint32_t w,uint32_t h,const std::vector<uint8_t>& rgba){if(!stbi_write_png(path.string().c_str(),w,h,4,rgba.data(),w*4))throw std::runtime_error("Cluster capture PNG write failed");}
}
void captureClusterScene(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    const char* capture=std::getenv("SCENERENDERER_CLUSTER_CAPTURE_DIR");if(!capture)return;
    if(std::getenv("SCENERENDERER_CLUSTER_BUILD_COMPARE")){const char* scene=std::getenv("SCENERENDERER_CLUSTER_DENSE_SCENE");captureClusterBuildComparison(device,directory,scene?scene:"sponza",capture);return;}
    if(const char* scene=std::getenv("SCENERENDERER_CLUSTER_DENSE_SCENE")){captureDenseClusterScene(device,directory,scene,capture);return;}
    std::ifstream prior(std::filesystem::path(capture)/"validation.json");nlohmann::json report;if(prior)prior>>report;
    if(capture){std::filesystem::path root(capture);std::filesystem::create_directories(root);
        auto scene=makeClassicScene("sponza");scene->mainCamera()->setAspect(16.f/9);SceneSnapshotBuilder builder;auto snapshot=builder.capture(scene,8,960,540);
        auto runtimeSnapshot=*snapshot;runtimeSnapshot.frame.clusters.enabled=true;
        SceneAdapter adapter(device);auto resolved=adapter.resolve(runtimeSnapshot);size_t expected=0,polls=0;
        for(const auto& draw:runtimeSnapshot.draws)if(draw.mesh&&!draw.subdivision&&!draw.wireframe&&draw.pathTracingKind==0&&draw.mesh->indices.size()/3>=128)++expected;
        auto started=std::chrono::steady_clock::now();
        while(std::count_if(resolved.packets.begin(),resolved.packets.end(),[](const auto& p){return bool(p.clusters);})<expected){
            if(std::chrono::steady_clock::now()-started>std::chrono::seconds(30))throw std::runtime_error("Async cluster build did not publish expected meshes");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));resolved=adapter.resolve(runtimeSnapshot);++polls;
        }
        double backgroundSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();auto frame=resolved.frame;frame.clusters.enabled=true;frame.ssao=false;frame.rsm=false;frame.ddgi.enabled=false;frame.taa=false;frame.clusters.twoPhase=false;
        ForwardPbrRenderer view(device,directory,960,540,PbrPath::Scene);
        nlohmann::json meshes=nlohmann::json::array();
        for(size_t i=0;i<resolved.packets.size();++i){const auto& draw=snapshot->draws[i];if(!draw.mesh||draw.subdivision||draw.pathTracingKind!=0)continue;
            if(!resolved.packets[i].clusters)continue;auto h=resolved.packets[i].clusters->hierarchy;
            meshes.push_back({{"mesh_id",draw.mesh->id},{"source_triangles",h->sourceIndices/3},{"leaf_clusters",h->leafClusters},{"all_clusters",h->clusters.size()},{"nodes",h->nodes.size()},{"roots",h->roots.size()},{"build_seconds",h->buildSeconds},{"resident_bytes",h->indices.size()*4+h->clusters.size()*48+h->nodes.size()*48}});
        }
        auto off=frame;off.clusters.enabled=false;view.render(off,resolved.packets,resolved.exposure);auto fullPixels=view.readOutput();png(root/"sponza-full.png",960,540,fullPixels);
        nlohmann::json cuts=nlohmann::json::array();
        for(float budget:{0.f,1.f,4.f,16.f}){frame.clusters.pixelError=budget;view.render(frame,resolved.packets,resolved.exposure);auto pixels=view.readOutput();png(root/("sponza-clusters-"+std::to_string(int(budget))+"px.png"),960,540,pixels);
            uint64_t input=0,selected=0,clusters=0;for(auto count:view.readClusterCullCounts()){input+=count.sourceTriangles;selected+=count.selectedTriangles;clusters+=count.selectedClusters;}
            cuts.push_back({{"pixel_error",budget},{"source_triangles",input},{"visible_triangles",selected},{"visible_clusters",clusters},{"display_pixels_equal_full",pixels==fullPixels},{"dispatches",view.clusterCullStats().dispatches}});
            auto debug=frame;debug.clusters.wireframe=true;debug.shadows=false;debug.sky=false;debug.ambient=.8f;debug.clouds.enabled=false;
            view.render(debug,resolved.packets,1.f);png(root/("sponza-wireframe-"+std::to_string(int(budget))+"px.png"),960,540,view.readOutput());

        }
        // The occlusion image compares the identical 4px cut, rather than full
        // geometry, so a LOD approximation cannot mask a false visibility cull.
        auto base4=frame;base4.clusters.pixelError=4;base4.clusters.wireframe=false;base4.clusters.occlusion=false;
        view.render(base4,resolved.packets,resolved.exposure);auto beforeHDR=view.readHDR();auto beforeDisplay=view.readOutput();auto base4Counts=view.readClusterCullCounts();
        auto hz=base4;hz.clusters.occlusion=true;view.render(hz,resolved.packets,resolved.exposure);auto afterHDR=view.readHDR();auto afterDisplay=view.readOutput();
        double worst=0;size_t different=0;for(size_t i=0;i<beforeHDR.size();++i){if(!std::isfinite(afterHDR[i]))throw std::runtime_error("Sponza cluster HiZ nonfinite HDR");worst=std::max(worst,double(std::abs(beforeHDR[i]-afterHDR[i])));}
        for(size_t i=0;i<beforeDisplay.size();i+=4)if(!std::equal(beforeDisplay.begin()+i,beforeDisplay.begin()+i+4,afterDisplay.begin()+i))++different;
        if(worst>.001||different)throw std::runtime_error("Sponza cluster HiZ changed visible pixels");
        uint64_t hzTriangles=0,hzClusters=0;for(auto c:view.readClusterCullCounts()){hzTriangles+=c.selectedTriangles;hzClusters+=c.selectedClusters;}
        uint64_t prepassActual=0;for(auto c:view.readHiZCullCounts())if(c.occluder){auto found=std::find_if(base4Counts.begin(),base4Counts.end(),[&](const auto& x){return x.packet==c.packet;});prepassActual+=found==base4Counts.end()?c.inputTriangles:found->selectedTriangles;}
        png(root/"sponza-clusters-4px-hiz.png",960,540,afterDisplay);
        report["cluster_hiz_sponza"]={{"hdr_max_error",worst},{"changed_display_pixels",different},{"selected_triangles",hzTriangles},{"selected_clusters",hzClusters},{"cluster_dispatches",view.clusterCullStats().dispatches},{"active",view.clusterCullStats().occlusion},{"actual_prepass_triangles",prepassActual},{"pyramid_levels",view.hizCullStats().pyramidLevels},{"prepass_source_triangle_budget_used",view.hizCullStats().prepassTriangles}};
        // Preserve a separate temporal renderer so reference frames cannot reset history.
        ForwardPbrRenderer temporalView(device,directory,960,540,PbrPath::Scene);
        auto temporalFrame=base4;temporalFrame.clusters.occlusion=true;temporalFrame.clusters.twoPhase=true;
        nlohmann::json temporalCases=nlohmann::json::array();glm::mat4 previousTemporalVP{1};auto temporalRoot=root/"two-stage";std::filesystem::create_directories(temporalRoot);
        auto matrix=[](const glm::mat4& m){std::vector<float> v;for(int c=0;c<4;++c)for(int r=0;r<4;++r)v.push_back(m[c][r]);return v;};
        auto rawFloat=[](const std::filesystem::path& path,const std::vector<float>& data){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()*4));if(!f)throw std::runtime_error("cluster float capture failed");};
        auto captureTemporal=[&](const char* name,const FrameData& f,bool intermediates){
            auto reference=f;reference.clusters.occlusion=false;reference.hizCulling=false;
            view.render(reference,resolved.packets,resolved.exposure);auto expectedHDR=view.readHDR();auto expectedDisplay=view.readOutput();auto expectedDepth=view.readDepth();
            temporalView.captureClusterIntermediates(intermediates);temporalView.render(f,resolved.packets,resolved.exposure);temporalView.captureClusterIntermediates(false);auto gotHDR=temporalView.readHDR();auto gotDisplay=temporalView.readOutput();auto gotDepth=temporalView.readDepth();
            if(!temporalView.clusterCullStats().twoPhase)throw std::runtime_error("Sponza two-stage did not execute");
            double error=0,depthError=0;size_t changed=0;
            for(size_t i=0;i<gotHDR.size();++i){if(!std::isfinite(gotHDR[i]))throw std::runtime_error("Sponza two-stage nonfinite HDR");error=std::max(error,double(std::abs(gotHDR[i]-expectedHDR[i])));}
            for(size_t i=0;i<gotDepth.size();++i)depthError=std::max(depthError,double(std::abs(gotDepth[i]-expectedDepth[i])));
            for(size_t i=0;i<gotDisplay.size();i+=4)if(!std::equal(gotDisplay.begin()+i,gotDisplay.begin()+i+4,expectedDisplay.begin()+i))++changed;
            nlohmann::json gbufferErrors=nlohmann::json::array();
            for(uint32_t a=0;a<6;++a){auto expected=view.readGBuffer(a),got=temporalView.readGBuffer(a);double maxError=0;for(size_t i=0;i<got.size();++i)maxError=std::max(maxError,double(std::abs(expected[i]-got[i])));gbufferErrors.push_back(maxError);if(maxError>.001)throw std::runtime_error("Sponza two-stage G-buffer differs");}
            if(error>.001||changed||depthError>1e-6)throw std::runtime_error("Sponza two-stage image/depth differs");
            uint64_t cut=0,early=0,late=0,rejected=0;auto flags=temporalView.readClusterVisibility();nlohmann::json clusterFlags=nlohmann::json::array();
            for(auto c:flags)if(c.selected){cut+=c.indexCount/3;if(c.early)early+=c.indexCount/3;if(c.late)late+=c.indexCount/3;if(!c.early&&!c.late)rejected+=c.indexCount/3;
                const auto& geometry=*resolved.packets[c.packet].clusters->hierarchy;const auto& bounds=geometry.clusters[c.cluster];
                clusterFlags.push_back({{"packet",c.packet},{"cluster",c.cluster},{"triangles",c.indexCount/3},{"early",c.early},{"late",c.late},{"low",{bounds.low.x,bounds.low.y,bounds.low.z}},{"high",{bounds.high.x,bounds.high.y,bounds.high.z}}});}
            uint64_t count=0;for(auto c:temporalView.readClusterCullCounts())count+=c.selectedTriangles;
            if(early+late!=count||cut!=early+late+rejected)throw std::runtime_error("Sponza cluster partition incorrect");
            nlohmann::json result={{"case",name},{"history_used",temporalView.clusterCullStats().historyUsed},{"cut_triangles",cut},{"early_triangles",early},{"late_triangles",late},{"rejected_triangles",rejected},{"hdr_max_error",error},{"depth_max_error",depthError},{"changed_display_pixels",changed},{"gbuffer_max_errors",gbufferErrors}};
            png(temporalRoot/(std::string(name)+"-reference.png"),960,540,expectedDisplay);png(temporalRoot/(std::string(name)+"-final.png"),960,540,gotDisplay);
            if(intermediates){
                auto caseRoot=temporalRoot/name;std::filesystem::create_directories(caseRoot);auto metadata=result;metadata["width"]=960;metadata["height"]=540;metadata["view_projection"]=matrix(f.viewProjection);metadata["history_view_projection"]=matrix(previousTemporalVP);metadata["clusters"]=clusterFlags;
                metadata["models"]=nlohmann::json::array();for(auto& p:resolved.packets)metadata["models"].push_back(matrix(p.model));metadata["pyramids"]=nlohmann::json::object();
                for(const char* which:{"current","history"}){auto levels=nlohmann::json::array();auto baseMip=temporalView.clusterPyramidBaseMip(std::string(which)=="history");metadata[std::string(which)+"_base_mip"]=baseMip;for(uint32_t mip=0;mip<temporalView.hizCullStats().pyramidLevels;++mip){auto values=std::string(which)=="current"?temporalView.readHiZDepth(mip):temporalView.readClusterHistoryDepth(mip);if(values.empty())break;
                        auto filename=std::string(which)+"-"+std::to_string(mip)+".f32";rawFloat(caseRoot/filename,values);levels.push_back({{"level",mip},{"width",std::max(960u>>(mip+baseMip),1u)},{"height",std::max(540u>>(mip+baseMip),1u)},{"file",filename}});}metadata["pyramids"][which]=levels;}
                rawFloat(caseRoot/"early-depth.f32",temporalView.readClusterEarlyDepth());rawFloat(caseRoot/"early-albedo.f32",temporalView.readClusterEarlyAlbedo());rawFloat(caseRoot/"final-albedo.f32",temporalView.readGBuffer(2));
                rawFloat(caseRoot/"final-depth.f32",gotDepth);rawFloat(caseRoot/"reference-depth.f32",expectedDepth);
                // Reconstruct each stage from its actual GPU membership and original
                // source vertex buffers. This diagnostic render does not participate in culling.
                auto diagnostic=f;diagnostic.clusters.enabled=false;diagnostic.hizCulling=false;diagnostic.objectCulling=false;diagnostic.shadows=false;diagnostic.sky=false;diagnostic.directionalEnabled=false;diagnostic.ambient=1;
                for(const char* stage:{"early","pending","late","rejected"}){std::vector<DrawPacket> stagePackets;
                    for(size_t packet=0;packet<resolved.packets.size();++packet){std::vector<uint32_t> indices;
                        for(auto c:flags)if(c.packet==packet&&c.selected){bool keep=std::string(stage)=="early"?c.early:std::string(stage)=="pending"?!c.early:std::string(stage)=="late"?c.late:!c.early&&!c.late;
                            if(keep){const auto& h=*resolved.packets[packet].clusters->hierarchy;const auto& cluster=h.clusters[c.cluster];indices.insert(indices.end(),h.indices.begin()+cluster.range.x,h.indices.begin()+cluster.range.x+cluster.range.y);}}
                        if(indices.empty())continue;auto p=resolved.packets[packet];p.mesh=std::make_shared<GpuMesh>(device,snapshot->draws[packet].mesh->vertices,indices);p.clusters.reset();stagePackets.push_back(std::move(p));}
                    view.render(diagnostic,stagePackets,resolved.exposure);png(caseRoot/(std::string(stage)+".png"),960,540,view.readOutput());}
                std::ofstream file(caseRoot/"metadata.json");file<<metadata.dump(2)<<'\n';
            }
            temporalCases.push_back(result);previousTemporalVP=f.viewProjection;return late;
        };
        captureTemporal("cold",temporalFrame,true);captureTemporal("warm",temporalFrame,true);
        auto move=temporalFrame;auto projection=temporalFrame.viewProjection*glm::inverse(temporalFrame.view);move.cameraPosition+=glm::vec3(0,0,2.0f);move.view=glm::lookAt(move.cameraPosition,move.cameraPosition+scene->mainCamera()->getFront(),scene->mainCamera()->getUp());move.viewProjection=projection*move.view;
        auto lateRecovery=captureTemporal("camera-strafe",move,true);if(!lateRecovery)throw std::runtime_error("Sponza camera strafe did not exercise late recovery");
        captureTemporal("camera-return",temporalFrame,true);
        auto coarseTemporal=temporalFrame;coarseTemporal.clusters.pixelError=16;captureTemporal("lod-16px",coarseTemporal,false);captureTemporal("lod-4px",temporalFrame,false);
        temporalView.resetTemporal();captureTemporal("reset",temporalFrame,false);
        report["two_stage_sponza"]={{"cases",temporalCases},{"scope","matched camera/LOD/exposure; exact display and depth plus six G-buffer attachments; GPU flag-derived diagnostic stage draws; no UI"}};
        if(std::getenv("SCENERENDERER_CLUSTER_BENCHMARK")){
            struct Mode {const char* name;bool enabled,batched,hiz;float error;uint32_t budget=250000;bool twoPhase=false,fusedPyramid=false,fusedLate=false,intervals=false,encoderBatch=true;};
            std::vector<Mode> modes{{{"full",false,true,false,4},{"legacy_1px",true,false,false,1},{"legacy_4px",true,false,false,4},
                {"batch_1px",true,true,false,1},{"batch_4px",true,true,false,4},{"batch_hiz_1px",true,true,true,1},{"batch_hiz_4px",true,true,true,4},{"batch_hiz_4px_16k",true,true,true,4,16000},{"batch_hiz_4px_32k",true,true,true,4,32000},{"batch_hiz_4px_64k",true,true,true,4,64000},{"two_stage_1px",true,true,true,1,250000,true,true,true},{"two_stage_4px",true,true,true,4,250000,true,true,true}}};
            if(!std::getenv("SCENERENDERER_CLUSTER_BENCHMARK_ALL_MODES"))modes={{"batch_separate",true,true,false,4,250000,false,false,false,false,false},{"two_stage_separate",true,true,true,4,250000,true,false,false,false,false},{"batch_4px",true,true,false,4},{"two_stage_original",true,true,true,4,250000,true},{"two_stage_pyramid",true,true,true,4,250000,true,true},{"two_stage_4px",true,true,true,4,250000,true,true,true},{"two_stage_intervals",true,true,true,4,250000,true,true,true,true}};
            nlohmann::json timing={{"scope","GPU early/late geometry + every clusters/ and hiz/ span (including prepass and pyramid); same build/camera/LOD/materials; 3 rounds with frame-interleaved rotated order, each 8 warmup + 24 measured; profiler overhead included; excludes shadows, lighting, tone mapping and CPU construction; CPU render call and first-cull-to-geometry-end GPU window (includes inter-encoder gaps) reported separately, not display latency"}};
            std::map<int,std::vector<ClusterCullCount>> referenceCounts{{4,base4Counts}};
            std::array<std::vector<double>,12> totals,cpuTimes,windows;std::array<nlohmann::json,12> raw;
            for(auto& r:raw)r=nlohmann::json::array();
            const char* encoderOption=std::getenv("SCENERENDERER_DISABLE_CLUSTER_ENCODER_BATCH");std::optional<std::string> originalEncoderOption=encoderOption?std::optional<std::string>(encoderOption):std::nullopt;
            std::vector<std::unique_ptr<ForwardPbrRenderer>> benchmarkViews;
            for(size_t i=0;i<modes.size();++i)benchmarkViews.push_back(std::make_unique<ForwardPbrRenderer>(device,directory,960,540,PbrPath::Scene));
            for(size_t round=0;round<3;++round)for(int sample=0;sample<32;++sample)for(size_t order=0;order<modes.size();++order){size_t mode=(order+round+sample)%modes.size();const auto& m=modes[mode];auto& activeView=*benchmarkViews[mode];auto benchmark=frame;
                benchmark.clusters.conservativeIntervals=m.intervals;benchmark.clusters.fusedPyramid=m.fusedPyramid;benchmark.clusters.fusedLate=m.fusedLate;benchmark.clusters.twoPhase=m.twoPhase;benchmark.clusters.enabled=m.enabled;benchmark.clusters.batched=m.batched;benchmark.clusters.occlusion=m.hiz;benchmark.clusters.wireframe=false;benchmark.clusters.pixelError=m.error;benchmark.hizCulling=false;benchmark.hizOccluderTriangleBudget=m.budget;
                if(m.encoderBatch)unsetenv("SCENERENDERER_DISABLE_CLUSTER_ENCODER_BATCH");else setenv("SCENERENDERER_DISABLE_CLUSTER_ENCODER_BATCH","1",1);
                device->drainGpuProfile();
                auto start=std::chrono::steady_clock::now();activeView.render(benchmark,resolved.packets,resolved.exposure);double cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                    activeView.readHDR();auto spans=device->drainGpuProfile();if(sample<8)continue;
                    double sum=0,first=std::numeric_limits<double>::infinity(),last=0;size_t n=0;for(const auto& t:spans)if(t.label.rfind("geometry",0)==0||t.label.rfind("clusters/",0)==0||t.label.rfind("hiz/",0)==0){double ms=t.endMilliseconds-t.startMilliseconds;sum+=ms;first=std::min(first,t.startMilliseconds);last=std::max(last,t.endMilliseconds);++n;raw[mode].push_back({{"round",round},{"sample",sample-8},{"stage",t.label},{"kind",t.kind},{"start_ms",t.startMilliseconds},{"end_ms",t.endMilliseconds},{"ms",ms}});}
                    if(n){totals[mode].push_back(sum);cpuTimes[mode].push_back(cpuMs);windows[mode].push_back(last-first);}
            }
            if(originalEncoderOption)setenv("SCENERENDERER_DISABLE_CLUSTER_ENCODER_BATCH",originalEncoderOption->c_str(),1);else unsetenv("SCENERENDERER_DISABLE_CLUSTER_ENCODER_BATCH");
            for(size_t mode=0;mode<modes.size();++mode){const auto& m=modes[mode];auto& activeView=*benchmarkViews[mode];
                uint64_t triangles=0,clusters=0;for(auto c:activeView.readClusterCullCounts()){triangles+=c.selectedTriangles;clusters+=c.selectedClusters;}
                if(m.hiz&&m.error==4){auto pixels=activeView.readOutput();auto hdr=activeView.readHDR();double maxError=0;
                    for(size_t i=0;i<hdr.size();++i)maxError=std::max(maxError,double(std::abs(hdr[i]-beforeHDR[i])));
                    if(pixels!=beforeDisplay||maxError>.001)throw std::runtime_error("Sponza HiZ budget changed visible surface");
                }
                uint64_t actualPrepass=0;
                if(m.enabled&&!m.hiz)referenceCounts[int(m.error)]=activeView.readClusterCullCounts();
                if(m.hiz)for(auto c:activeView.readHiZCullCounts())if(c.occluder){const auto& reference=referenceCounts.at(int(m.error));auto found=std::find_if(reference.begin(),reference.end(),[&](const auto& x){return x.packet==c.packet;});actualPrepass+=found==reference.end()?c.inputTriangles:found->selectedTriangles;}
                uint64_t earlyTriangles=0,lateTriangles=0;for(auto c:activeView.readClusterVisibility()){if(c.early)earlyTriangles+=c.indexCount/3;if(c.late)lateTriangles+=c.indexCount/3;}
                timing[m.name]={{"supported",!totals[mode].empty()},{"selected_triangles",m.enabled?nlohmann::json(triangles):nlohmann::json(nullptr)},
                    {"selected_clusters",clusters},{"early_triangles",earlyTriangles},{"late_triangles",lateTriangles},{"cluster_dispatches",activeView.clusterCullStats().dispatches},{"batched",activeView.clusterCullStats().batched},{"two_phase",activeView.clusterCullStats().twoPhase},{"cluster_occlusion",activeView.clusterCullStats().occlusion},{"actual_prepass_triangles",actualPrepass},{"occluder_triangle_budget",m.budget},{"cluster_buffer_bytes",activeView.clusterCullStats().bufferBytes},
                    {"pyramid_levels",activeView.hizCullStats().pyramidLevels},{"prepass_source_triangle_budget_used",activeView.hizCullStats().prepassTriangles}};
            }
            for(size_t i=0;i<modes.size();++i){auto& record=timing[modes[i].name];std::sort(totals[i].begin(),totals[i].end());std::sort(cpuTimes[i].begin(),cpuTimes[i].end());std::sort(windows[i].begin(),windows[i].end());record["samples_ms"]=totals[i];record["cpu_render_samples_ms"]=cpuTimes[i];record["raw"]=raw[i];record["gpu_window_samples_ms"]=windows[i];
                if(!totals[i].empty()){auto n=totals[i].size();record["median_ms"]=(totals[i][(n-1)/2]+totals[i][n/2])*.5;record["gpu_window_median_ms"]=(windows[i][(n-1)/2]+windows[i][n/2])*.5;record["cpu_render_median_ms"]=(cpuTimes[i][(n-1)/2]+cpuTimes[i][n/2])*.5;}
            }
            std::ofstream out(root/"benchmark.json");out<<timing.dump(2)<<'\n';
            // Matched moving-camera sequence: preserve a renderer's history and
            // put independent reference renders outside every measured interval.
            ForwardPbrRenderer motionReference(device,directory,960,540,PbrPath::Scene);
            std::array<std::unique_ptr<ForwardPbrRenderer>,3> motionViews;for(auto& v:motionViews)v=std::make_unique<ForwardPbrRenderer>(device,directory,960,540,PbrPath::Scene);
            const std::array<const char*,3> names={"batch_4px","two_stage_original","two_stage_4px"};
            nlohmann::json movingTiming={{"scope","3 frame-interleaved rotated rounds; 8 moving warmups + 24 measured moving frames per mode per round; identical camera sequence; all clusters/ and hiz/ plus early/late geometry; reference renders/readbacks excluded; HDR/depth/display checked every measured frame"}};
            std::array<std::vector<double>,3> movingSums,movingWindows,movingCPU;std::array<nlohmann::json,3> movingRaw,movingCounts;
            for(auto& data:movingRaw)data=nlohmann::json::array();for(auto& data:movingCounts)data=nlohmann::json::array();
            for(size_t round=0;round<3;++round){for(auto& v:motionViews)v->resetTemporal();
                for(int sample=0;sample<32;++sample)for(size_t order=0;order<3;++order){size_t mode=(order+round+sample)%3;auto& motionView=*motionViews[mode];auto f=base4;f.clusters.occlusion=mode!=0;f.clusters.twoPhase=mode!=0;f.clusters.conservativeIntervals=false;f.clusters.fusedPyramid=mode==2;f.clusters.fusedLate=mode==2;
                    f.cameraPosition=base4.cameraPosition+glm::vec3(0,0,.75f*std::sin(float(sample)*.4f));f.view=glm::lookAt(f.cameraPosition,f.cameraPosition+scene->mainCamera()->getFront(),scene->mainCamera()->getUp());f.viewProjection=projection*f.view;
                    device->drainGpuProfile();auto start=std::chrono::steady_clock::now();motionView.render(f,resolved.packets,resolved.exposure);double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                    auto hdr=motionView.readHDR();auto spans=device->drainGpuProfile();if(sample<8)continue;
                    double sum=0,first=std::numeric_limits<double>::infinity(),last=0;
                    for(auto t:spans)if(t.label.rfind("geometry",0)==0||t.label.rfind("clusters/",0)==0||t.label.rfind("hiz/",0)==0){double ms=t.endMilliseconds-t.startMilliseconds;sum+=ms;first=std::min(first,t.startMilliseconds);last=std::max(last,t.endMilliseconds);movingRaw[mode].push_back({{"round",round},{"sample",sample-8},{"stage",t.label},{"kind",t.kind},{"start_ms",t.startMilliseconds},{"end_ms",t.endMilliseconds},{"ms",ms}});}
                    movingSums[mode].push_back(sum);movingWindows[mode].push_back(last-first);movingCPU[mode].push_back(cpu);
                    uint64_t cut=0,early=0,late=0;for(auto c:motionView.readClusterVisibility())if(c.selected){cut+=c.indexCount/3;if(c.early)early+=c.indexCount/3;if(c.late)late+=c.indexCount/3;}
                    auto pixels=motionView.readOutput();auto depth=motionView.readDepth();auto ref=f;ref.clusters.occlusion=false;ref.hizCulling=false;motionReference.render(ref,resolved.packets,resolved.exposure);auto expectedHDR=motionReference.readHDR(),expectedDepth=motionReference.readDepth();
                    if(pixels!=motionReference.readOutput()||depth!=expectedDepth)throw std::runtime_error("Sponza moving-camera display/depth mismatch");double error=0;for(size_t i=0;i<hdr.size();++i)error=std::max(error,double(std::abs(hdr[i]-expectedHDR[i])));if(error>.001)throw std::runtime_error("Sponza moving-camera HDR mismatch");
                    movingCounts[mode].push_back({{"round",round},{"sample",sample-8},{"cut_triangles",cut},{"early_triangles",early},{"late_triangles",late},{"hdr_max_error",error},{"depth_and_display_exact",true}});
                }
            }
            for(size_t mode=0;mode<3;++mode){auto median=[](std::vector<double> v){std::sort(v.begin(),v.end());return (v[(v.size()-1)/2]+v[v.size()/2])*.5;};movingTiming[names[mode]]={{"samples",movingSums[mode].size()},{"median_ms",median(movingSums[mode])},{"gpu_window_median_ms",median(movingWindows[mode])},{"cpu_render_median_ms",median(movingCPU[mode])},{"raw",movingRaw[mode]},{"frame_counts",movingCounts[mode]}};}
            std::ofstream motionFile(root/"moving-benchmark.json");motionFile<<movingTiming.dump(2)<<'\n';

        }
        if(std::getenv("SCENERENDERER_CLUSTER_FLOW"))captureClusterFlow(device,directory,capture,*snapshot,resolved);
        report["sponza"]={{"size",{960,540}},{"meshes",meshes},{"background_build_wall_seconds",backgroundSeconds},{"nonblocking_resolve_polls",polls},{"cuts",cuts},{"scope","native realtime PBR; same camera/exposure; TAA/AO/DDGI off; cluster selection and frustum culling on"}};scene->destroy();
    }
    std::ofstream out(std::filesystem::path(capture)/"validation.json");out<<report.dump(2)<<'\n';
    std::cout<<report.at("sponza").at("cuts").dump(2)<<'\n';
}
}
