#include "PT/CpuPathTracer.h"
#include "PT/GpuPathTracer.h"
#include "PT/ProceduralCapture.h"
#include "PT/ValidationScenes.h"
#include "engine/RenderRuntime.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/RasterInstanceBatcher.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/rhi/MaterialWetness.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stb/stb_image_write.h>
#include <json/json.hpp>
namespace pt {
namespace {
void validateRealtimeGgx(std::shared_ptr<rhi::GraphicsDevice> device) {
    const auto directory=rhi::defaultShaderDirectory();
    render::ForwardPbrRenderer forward(device,directory,64,64,render::PbrPath::Forward);
    render::ForwardPbrRenderer deferred(device,directory,64,64,render::PbrPath::Deferred);
    render::ForwardPbrRenderer scene(device,directory,64,64,render::PbrPath::Scene);
    render::SceneAdapter adapter(device);
    double maxRelative=0;size_t compared=0;
    for(uint32_t model:{0u,2u})for(float metallic:{0.f,1.f})for(int variant=0;variant<4;++variant){
        auto mesh=std::make_shared<render::MeshPayload>();mesh->id=99001;mesh->revision=variant+1;
        mesh->vertices={{{-.9f,-.9f,.4f},{0,0,1},{0,1}},{{.9f,-.9f,.4f},{0,0,1},{1,1}},
                        {{.9f,.9f,.4f},{0,0,1},{1,0}},{{-.9f,.9f,.4f},{0,0,1},{0,0}}};
        mesh->indices={0,1,2,0,2,3};
        if(variant>=2)mesh->pathTracingTangents.assign(4,glm::vec4(1,0,0,-1));
        auto payload=std::make_shared<render::MaterialPayload>();payload->id=99002;payload->revision=variant+1;
        if(variant==3)payload->images[1]=std::make_shared<const render::ImageRGBA8>(render::ImageRGBA8{1,1,{140,136,253,255}});
        render::SnapshotDraw draw;draw.objectId=1;draw.mesh=mesh;draw.material=payload;
        draw.parameters.albedoAlpha={.8f,.65f,.3f,1};draw.parameters.factors={metallic,.45f,1,0};
        draw.parameters.emissiveNormal.w=variant==3?.5f:0;
        draw.pathTracingBsdfModel=model;
        draw.pathTracingSpecularAnisotropy=variant==0?0:variant==3?-.8f:.8f;
        draw.pathTracingSpecularRotation=variant==2?.43f:variant==3?1.57079632679f:0;
        if(variant==2)draw.extension.features={int(render::MaterialFeature::RainWetness),40000,0,0};
        if(variant==3){draw.extension.features.x|=int(render::MaterialFeature::SpatialRainWetness);
            for(size_t cell=0;cell<256;++cell)render::setRainWetCell(draw.extension,cell,uint8_t((cell%16)*17));}
        draw.extension.specularShape={draw.pathTracingSpecularAnisotropy,draw.pathTracingSpecularRotation,model==2?2.f:1.f,0};
        if(variant>=2)draw.model=glm::scale(glm::mat4(1),glm::vec3(-.8f,.65f,1.4f));
        render::RenderWorldSnapshot world;world.draws={draw};auto& frame=world.frame;
        frame.cameraPosition={0,0,3};frame.ambient=0;frame.sky=false;frame.shadows=false;frame.ssao=false;frame.rsm=false;frame.taa=false;
        const auto L=glm::normalize(glm::vec3(.25f,.1f,1));const glm::vec3 radiance(2,1.7f,1.5f);
        frame.lights={{{0,0,0,0},glm::vec4(radiance,0),glm::vec4(-L,0)}};
        frame.viewportWidth=frame.viewportHeight=64;
        auto resolved=adapter.resolve(world);CpuScene cpu(world);
        auto compare=[&](render::ForwardPbrRenderer& renderer,bool useForward){
            auto f=resolved.frame;f.forwardShading=useForward;renderer.render(f,resolved.packets);
            const auto pixels=renderer.readHDR();for(auto value:pixels)if(!std::isfinite(value))throw std::runtime_error("Raster GGX nonfinite output");
            for(int y:{22,30,40})for(int x:{22,34,40}){
                glm::vec3 point((x+.5f)/32-1,1-(y+.5f)/32,draw.model[2].z*.4f);
                const auto V=glm::normalize(frame.cameraPosition-point);Surface hit;
                if(!cpu.intersect(frame.cameraPosition,-V,.001f,100,hit))throw std::runtime_error("Raster GGX reference ray missed");
                const auto expected=evaluateBsdf(hit,V,L)*surfaceCosine(hit,L)*radiance;
                const size_t at=size_t(y*64+x)*4;
                for(int c=0;c<3;++c){const double error=std::abs(pixels[at+c]-expected[c]),relative=error/std::max(.02f,std::abs(expected[c]));
                    maxRelative=std::max(maxRelative,relative);++compared;
                    if(error>.004+.012*std::abs(expected[c]))throw std::runtime_error("Raster matched GGX differs from CPU PT BSDF (model="+std::to_string(model)+", variant="+std::to_string(variant)+", error="+std::to_string(error)+")");
                }
            }
        };
        compare(forward,false);compare(deferred,false);compare(scene,false);compare(scene,true);
        // Same immutable material payload with changed scalars must invalidate the Raster cache.
        world.draws[0].extension.specularShape.y+=.2f;auto changed=adapter.resolve(world);
        if(changed.packets[0].material==resolved.packets[0].material)throw std::runtime_error("Raster GGX parameter edit retained stale material");
        // Ordinary instance batching uses the same transformed corner frames.
        if(variant==2 && metallic==1 && model==0){
            auto streamedWorld=world;streamedWorld.asynchronousStreaming=true;streamedWorld.draws[0]=draw;
            auto streamedMesh=std::make_shared<render::MeshPayload>(*mesh);streamedMesh->id=99003;streamedWorld.draws[0].mesh=streamedMesh;
            render::MeshUploadBudget budget;budget.bytesPerFrame=64;budget.bytesPerChunk=32;budget.cpuMilliseconds=100;
            adapter.setMeshUploadBudget(budget);bool published=false;size_t bytes=0;
            for(int step=0;step<16;++step){const auto staged=adapter.resolve(streamedWorld);bytes+=staged.meshUploadBytes;
                if(staged.meshUploadBytes>64)throw std::runtime_error("Tangent upload escaped Raster frame budget");
                if(!staged.packets.empty()){if(!staged.packets[0].mesh->ready() || staged.packets[0].mesh->materialTangentBytes()!=80)throw std::runtime_error("Raster published an incomplete tangent stream");published=true;break;}}
            if(!published || bytes!=216)throw std::runtime_error("Raster failed bounded tangent upload publication");
            auto instanced=resolved.packets;
            instanced[0].mesh=render::GpuMesh::withInstances(instanced[0].mesh,{draw.model});instanced[0].model=glm::mat4(1);
            forward.render(resolved.frame,resolved.packets);const auto baseline=forward.readHDR();forward.render(resolved.frame,instanced);const auto actual=forward.readHDR();
            for(size_t i=0;i<actual.size();++i)if(std::abs(actual[i]-baseline[i])>.004+.004*std::abs(baseline[i]))throw std::runtime_error("Instanced GGX frame differs from object transform");
        }
    }
    // An actual Raster appearance fixture, independent of external scene assets.
    auto sphere=std::make_shared<render::MeshPayload>();sphere->id=99101;sphere->revision=1;
    constexpr int rows=32,columns=64;constexpr float pi=3.14159265359f;
    for(int y=0;y<=rows;++y)for(int x=0;x<=columns;++x){
        const float theta=pi*y/rows,phi=2*pi*x/columns;const glm::vec3 N(std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi));
        sphere->vertices.push_back({N*.72f,N,{float(x)/columns,float(y)/rows}});
        sphere->pathTracingTangents.emplace_back(-std::sin(phi),0,std::cos(phi),1);
    }
    for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){uint32_t a=y*(columns+1)+x,b=a+columns+1;
        sphere->indices.insert(sphere->indices.end(),{a,a+1,b,a+1,b+1,b});}
    auto gold=std::make_shared<render::MaterialPayload>();gold->id=99102;gold->revision=1;
    render::RenderWorldSnapshot preview;
    for(int i=0;i<3;++i){render::SnapshotDraw d;d.objectId=i+1;d.shading=i;d.mesh=sphere;d.material=gold;
        d.parameters.albedoAlpha={.98f,.82f,.55f,1};d.parameters.factors={1,.4f,1,0};d.parameters.emissiveNormal.w=0;
        d.extension.specularShape={i==0?0:.8f,i==2?pi/2:0,1,0};d.model=glm::translate(glm::mat4(1),glm::vec3((i-1)*1.8f,0,0));preview.draws.push_back(d);}
    auto& pf=preview.frame;pf.cameraPosition={0,0,5};pf.view=glm::lookAt(pf.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));
    glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;
    pf.viewProjection=correction*glm::ortho(-3.f,3.f,-1.f,1.f,.1f,10.f)*pf.view;
    pf.ambient=.015f;pf.lights={{{0,0,0,0},{5,5,5,0},{-.25f,-.4f,-1,0}}};pf.shadows=false;pf.ssao=false;pf.rsm=false;pf.taa=false;
    auto fixture=adapter.resolve(preview);forward.resize(480,160);forward.render(fixture.frame,fixture.packets);const auto image=forward.readOutput();
    const auto name=directory+"/../"+(device->backend()==rhi::Backend::Metal?"metal":"vulkan")+"-realtime-ggx.png";
    if(!stbi_write_png(name.c_str(),480,160,4,image.data(),480*4))throw std::runtime_error("Cannot save Raster GGX fixture");
    nlohmann::json report{{"backend",device->backend()==rhi::Backend::Metal?"Metal":"Vulkan"},{"compared_linear_hdr_channels",compared},
        {"max_relative_error_floor_0_02",maxRelative},{"absolute_tolerance",.004},{"relative_tolerance",.012},
        {"coverage",{"PT PBR / GGX-add", "metal / dielectric reflection", "isotropic / signed anisotropic axes / rotation", "UV / explicit corner frames", "normal map", "mirror / nonuniform transform", "instance frames", "material scalar cache invalidation", "bounded asynchronous tangent upload", "Forward / Deferred / Scene deferred / Scene forward"}},
        {"preview_size",{480,160}},{"preview_lighting","directional light + constant ambient; Raster tone mapped"}};
    std::ofstream record(name+".json");record<<report.dump(2)<<'\n';if(!record)throw std::runtime_error("Cannot save Raster GGX validation report");
    std::cout << "PT/Raster matched GGX: " << compared << " direct-light channels, max relative error " << maxRelative
              << "; forward/deferred/scene, UV/corner frames, signed axes/rotation, normal map, mirror/nonuniform scale, scalar edits and instances passed\n";
}
}
void validatePathTracingBridge(std::shared_ptr<rhi::GraphicsDevice> device) {
    if(!device->computeLimits().supported || device->backend()==rhi::Backend::OpenGL)return;
    validateRealtimeGgx(device);
    render::validateRasterInstanceBatching(device,rhi::defaultShaderDirectory());
    render::FrameData frame;frame.sky=true;frame.cameraPosition={0,2,0};frame.lights.push_back({{0,0,0,0},{3,2.5f,2,0},{0,-1,-.3f,0}});
    engine::RenderRuntime runtime(device,nullptr,2,true);
    auto baked=runtime.captureAtmosphere(frame);
    auto source=makeCausticsScene(32,24,false).snapshot;source.frame.timeSeconds=1;
    render::OceanSurfaceSettings water;water.spectrum.size=64;water.spectrum.length=32;water.spectrum.amplitude=.001f;water.meshSize=17;water.seaLevel=.4f;water.detailWaves=false;water.absorption={.2f,.1f,.05f};water.scattering=glm::vec3(0);source.frame.oceans={water};
    CaptureOptions capture;capture.grass=false;capture.textureExtent=64;
    auto frozen=runtime.capturePathTracingScene(source,capture);source.frame.timeSeconds=2;auto later=runtime.capturePathTracingScene(source,capture);
    float motion=0;const auto &a=frozen.draws.back().mesh->vertices,&b=later.draws.back().mesh->vertices;for(size_t i=0;i<a.size();++i)motion=std::max(motion,glm::length(a[i].position-b[i].position));
    if(motion<1e-6f||frozen.draws.back().pathTracingIor!=1.333f||!frozen.frame.oceans.empty())throw std::runtime_error("PT: native FFT ocean capture did not freeze motion/optics");
    source.frame.oceans[0].spectrum.size=7;bool rejected=false;try{runtime.capturePathTracingScene(source,capture);}catch(const std::invalid_argument &){rejected=true;}if(!rejected)throw std::runtime_error("PT: capture error did not reach logic thread");
    std::shared_ptr<const render::RenderWorldSnapshot> exportedWorld;
    Options exportedOptions;
    const auto exportedPrefix=(std::filesystem::temp_directory_path()/(device->backend()==rhi::Backend::Metal?"pt-viewport-native-metal":"pt-viewport-native-vulkan")).string();
    {
        auto viewportWorld=std::make_shared<render::RenderWorldSnapshot>(makeCausticsScene(32,24,false).snapshot);
        viewportWorld->frame.viewportWidth=32;viewportWorld->frame.viewportHeight=24;
        viewportWorld->frame.shadows=false;viewportWorld->frame.ssao=false;viewportWorld->frame.rsm=false;viewportWorld->frame.taa=false;
        auto request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=1;request->world=viewportWorld;request->preparationByteBudget=1024;
        request->options.width=32;request->options.height=24;request->options.samples=16;request->options.gpuBatchSamples=1;request->options.adaptive=false;request->paused=true;
        bool navigation=false;
        std::shared_ptr<engine::PathTracingViewportExport> exportTicket;
        auto pump=[&](auto predicate,bool allowError=false) {
            // A cold native megakernel pipeline can exceed 15 s under compiler
            // load. This deadline detects a stalled job, not a frame-time budget.
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            while(std::chrono::steady_clock::now()<deadline) {
                auto status=runtime.pathTracingStatus();
                if(!allowError&&status.state=="Error"&&status.epoch==request->epoch&&!request->cancelled.load())throw std::runtime_error("PT viewport: "+status.message);
                if(predicate(status))return status;
                engine::RenderPacket packet;packet.world=viewportWorld;packet.pathTracingViewport=request;packet.pathTracingExport=exportTicket;packet.pathTracingNavigation=navigation;runtime.trySubmitFrame(std::move(packet));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto status=runtime.pathTracingStatus();
            throw std::runtime_error("PT viewport validation timed out: epoch="+std::to_string(status.epoch)+" state="+status.state+" samples="+std::to_string(status.samples)+" frames="+std::to_string(runtime.framesRendered()));
        };
        auto uploading=pump([](const auto &s){return s.epoch==1&&s.state=="Uploading"&&s.preparationBytes>0&&s.preparationBytes<s.preparationTotalBytes;});
        const auto rasterTarget=runtime.framesRendered()+3,preparationTarget=uploading.preparationSteps+3;
        auto partial=pump([&](const auto &s){return s.epoch==1&&s.state=="Uploading"&&s.preparationSteps>=preparationTarget&&runtime.framesRendered()>=rasterTarget;});
        if(partial.samples||partial.lastPreparationWorkBytes>1024||partial.gpuCacheBytes)throw std::runtime_error("PT viewport partially prepared film sampled / exceeded budget / published cache");
        if(!partial.previewFrames||!partial.previewFrozen)throw std::runtime_error("PT preparation did not present a frozen clean preview");
        auto cancelledUpload=request;cancelledUpload->cancelled=true;
        request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=2;request->world=viewportWorld;request->options=cancelledUpload->options;request->paused=true;request->preparationByteBudget=1024;
        engine::RenderPacket oldUpload;oldUpload.world=viewportWorld;oldUpload.pathTracingViewport=cancelledUpload;runtime.trySubmitFrame(std::move(oldUpload));
        auto firstReady=pump([](const auto &s){return s.epoch==2&&s.state=="Paused"&&s.samples==0;});
        if(firstReady.sceneUploads!=14||firstReady.sceneCacheHits||firstReady.preparationSteps<3||firstReady.preparationBytes!=firstReady.preparationTotalBytes)throw std::runtime_error("PT viewport cancelled upload contaminated replacement / incomplete publication");

        request->paused=false;pump([](const auto &s){return s.epoch==2&&s.samples>0;});request->paused=true;
        auto paused=pump([](const auto &s){return s.epoch==2&&(s.state=="Paused"||s.state=="Complete");});
        if(denoiserAvailable()){
            const auto samples=paused.samples;paused=pump([&](const auto &s){return s.epoch==2&&s.denoisedSamples==samples&&!s.denoiserBusy;});
            if(paused.denoiserContext.deviceBuilds!=1||paused.denoiserContext.filterBuilds!=3)throw std::runtime_error("PT viewport did not retain its CPU OIDN context");
        }
        request->denoise=false;
        auto resized=std::make_shared<render::RenderWorldSnapshot>(*viewportWorld);resized->frame.viewportWidth=48;resized->exposure=3;viewportWorld=resized;
        const auto frameTarget=runtime.framesRendered()+3;
        auto after=pump([&](const auto &s){return s.epoch==2&&runtime.framesRendered()>=frameTarget;});
        if(after.samples!=paused.samples||after.filmReadbacks!=paused.filmReadbacks)throw std::runtime_error("PT viewport resize/exposure/raw toggle advanced or read back paused film");
        request->denoise=true;
        auto stale=request;stale->cancelled=true;
        request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=3;request->world=viewportWorld;
        request->options.width=32;request->options.height=24;request->options.samples=4;request->options.gpuBatchSamples=1;request->options.adaptive=false;request->paused=true;
        engine::RenderPacket stalePacket;stalePacket.world=viewportWorld;stalePacket.pathTracingViewport=stale;runtime.trySubmitFrame(std::move(stalePacket));
        pump([](const auto &s){return s.epoch==3&&s.state=="Paused"&&s.samples==0;});
        request->paused=false;pump([](const auto &s){return s.epoch==3&&s.state=="Complete"&&s.samples==4;});
        exportTicket=std::make_shared<engine::PathTracingViewportExport>();exportTicket->epoch=3;exportTicket->prefix=exportedPrefix;
        auto exported=exportTicket->completion->get_future();
        pump([&](const auto &s){return s.epoch==3&&exported.wait_for(std::chrono::seconds(0))==std::future_status::ready;});
        if(exported.get()!=exportedPrefix)throw std::runtime_error("PT viewport export completion lost output prefix");
        exportedWorld=viewportWorld;exportedOptions=request->options;exportTicket.reset();
        navigation=true;const auto navigationTarget=runtime.framesRendered()+3;
        auto navigating=pump([&](const auto &s){return s.epoch==3&&s.state=="Navigating"&&runtime.framesRendered()>=navigationTarget;});
        if(navigating.samples!=4)throw std::runtime_error("PT navigation changed the retained film");
        auto cameraWorld=std::make_shared<render::RenderWorldSnapshot>(*viewportWorld);cameraWorld->frame.cameraPosition.x+=.2f;
        cameraWorld->frame.viewProjection=cameraWorld->frame.viewProjection*glm::translate(glm::mat4(1),glm::vec3(-.2f,0,0));viewportWorld=cameraWorld;
        auto previous=request;previous->cancelled=true;
        engine::RenderPacket cancelledPacket;cancelledPacket.world=viewportWorld;cancelledPacket.pathTracingViewport=previous;runtime.trySubmitFrame(std::move(cancelledPacket));
        request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=4;request->world=viewportWorld;request->options=previous->options;request->forceRecapture=false;request->paused=true;navigation=false;
        auto cameraReady=pump([](const auto &s){return s.epoch==4&&s.state=="Paused"&&s.samples==0;});
        if(cameraReady.sceneUploads!=0||cameraReady.sceneUploadBytes!=0||cameraReady.sceneCacheHits!=0)throw std::runtime_error("Camera-only viewport request prepared/uploaded scene again");
        request->paused=false;pump([](const auto &s){return s.epoch==4&&s.state=="Complete";});
        auto materialWorld=std::make_shared<render::RenderWorldSnapshot>(*viewportWorld);materialWorld->draws[0].parameters.albedoAlpha.x*=.7f;viewportWorld=materialWorld;
        previous=request;previous->cancelled=true;request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=5;request->world=viewportWorld;request->options=previous->options;request->forceRecapture=false;request->paused=true;
        auto materialReady=pump([](const auto &s){return s.epoch==5&&s.state=="Paused";});
        if(materialReady.tlasReuses!=1||materialReady.tlasBuilds||materialReady.tlasRefits||materialReady.emitterReuses!=1)throw std::runtime_error("Material-only viewport update rebuilt derived acceleration");
        if(materialReady.sceneUploads!=1||materialReady.sceneCacheHits!=13)throw std::runtime_error("Material-only viewport update uploaded unrelated scene buffers");
        auto transformWorld=std::make_shared<render::RenderWorldSnapshot>(*viewportWorld);transformWorld->draws[0].model[3].x+=.01f;viewportWorld=transformWorld;
        previous=request;previous->cancelled=true;request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=7;request->world=viewportWorld;request->options=previous->options;request->forceRecapture=false;request->paused=true;
        auto transformReady=pump([](const auto &s){return s.epoch==7&&s.state=="Paused";});
        if(transformReady.tlasRefits!=1||transformReady.tlasBuilds||transformReady.sceneUploads!=2)throw std::runtime_error("Transform viewport update did not refit / upload only changed bounds and instances");
        request->paused=false;pump([](const auto &s){return s.epoch==7&&s.state=="Complete";});
        if(denoiserAvailable()){
            pump([](const auto &s){return s.epoch==7&&s.filmReadbacks>0;});
            auto nextWorld=std::make_shared<render::RenderWorldSnapshot>(*viewportWorld);nextWorld->frame.cameraPosition.x+=.1f;nextWorld->frame.viewProjection=nextWorld->frame.viewProjection*glm::translate(glm::mat4(1),glm::vec3(-.1f,0,0));viewportWorld=nextWorld;
            previous=request;previous->cancelled=true;request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=8;request->world=viewportWorld;request->options=previous->options;request->forceRecapture=false;request->paused=true;request->denoise=false;
            auto fresh=pump([](const auto &s){return s.epoch==8&&s.state=="Paused"&&!s.denoiserBusy;});
            if(fresh.denoisedSamples||fresh.filmReadbacks||fresh.samples)throw std::runtime_error("PT viewport published an old epoch denoise result");
            request->paused=false;pump([](const auto &s){return s.epoch==8&&s.state=="Complete";});const auto rawTarget=runtime.framesRendered()+3;
            auto rawOnly=pump([&](const auto &s){return s.epoch==8&&runtime.framesRendered()>=rawTarget;});if(rawOnly.filmReadbacks)throw std::runtime_error("PT raw-only mode read back film automatically");
            request->denoise=true;auto filtered=pump([](const auto &s){return s.epoch==8&&s.denoisedSamples==4&&!s.denoiserBusy;});
            if(filtered.filmReadbacks!=1||filtered.denoiserContext.deviceBuilds!=1||filtered.denoiserContext.filterBuilds!=3)throw std::runtime_error("PT final checkpoint repeated readback / rebuilt OIDN context");
        }
        auto lighting=std::make_shared<engine::PathTracingViewportLighting>();lighting->environment=std::make_shared<Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.3f)));
        auto oldLightingRequest=request;oldLightingRequest->cancelled=true;request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=9;request->world=viewportWorld;request->options=oldLightingRequest->options;request->forceRecapture=false;request->paused=true;request->denoise=false;request->lighting=lighting;
        auto imported=pump([](const auto &s){return s.epoch==9&&s.state=="Paused";});
        if(!imported.preparationSteps||imported.sceneUploads!=1||imported.readyLatencySeconds<imported.requestAcceptedSeconds||!imported.cpuSceneSeconds||!imported.cpuExportSeconds||!imported.startupWarmupError.empty())throw std::runtime_error("PT frozen lighting did not invalidate reuse / record runtime stages");
        auto replacement=std::make_shared<engine::PathTracingViewportLighting>(*lighting);replacement->sunDirection=glm::normalize(glm::vec3(.2f,1,.3f));replacement->sunIrradiance={3,3,3};replacement->sunRadius=.00465f;
        request->cancelled=true;request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=10;request->world=viewportWorld;request->options=oldLightingRequest->options;request->forceRecapture=false;request->paused=true;request->denoise=false;request->lighting=replacement;
        auto relit=pump([](const auto &s){return s.epoch==10&&s.state=="Paused";});
        if(!relit.preparationSteps||relit.sceneUploads||relit.sceneCacheHits!=14)throw std::runtime_error("PT replaced sun owner reused stale lighting or uploaded scene buffers");
        exportTicket=std::make_shared<engine::PathTracingViewportExport>();exportTicket->epoch=10;exportTicket->prefix=exportedPrefix+"-cancelled";
        auto cancelledExport=exportTicket->completion->get_future();request->cancelled=true;
        pump([&](const auto &s){return s.state=="Raster"&&cancelledExport.wait_for(std::chrono::seconds(0))==std::future_status::ready;});
        bool exportRejected=false;try{cancelledExport.get();}catch(const std::runtime_error &){exportRejected=true;}
        if(!exportRejected)throw std::runtime_error("Discarded PT packet retained / accepted an uncaptured stale export");exportTicket.reset();
        request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=6;request->world=viewportWorld;request->options.maxDepth=0;
        auto failure=pump([](const auto &s){return s.epoch==6&&s.state=="Error";},true);
        if(failure.message.empty())throw std::runtime_error("PT viewport preparation error was lost");
        request->cancelled=true;pump([](const auto &s){return s.state=="Raster";});
        std::cout<<"Render-thread PT viewport: async prepare, per-frame budget / Raster while uploading / cancelled upload isolation, camera/material cache reuse, TLAS refit, navigation, pause/resume, resize/exposure, stale epoch, completion, persistent CPU OIDN / raw toggle / bounded readbacks / old filter epoch / linear export, cancellation and error recovery passed\n";
    }
    runtime.finish();
    {
        CpuScene expected(*exportedWorld);const auto reference=renderGpu(expected,exportedOptions,device),raw=readPfm(exportedPrefix+".pfm");
        if(raw.radiance!=reference.radiance)throw std::runtime_error("PT viewport linear export differs from unchanged raw GPU transport");
        std::ifstream reportFile(exportedPrefix+".json");nlohmann::json report;reportFile>>report;
        if(report["epoch"]!=3||report["samples"]!=4||report["seed"]!=exportedOptions.seed)throw std::runtime_error("PT viewport export did not freeze epoch / sample metadata");
        if(denoiserAvailable()&&(report["denoise_device"]!="CPU"||readPfm(exportedPrefix+"-denoised.pfm").radiance.size()!=raw.radiance.size()))throw std::runtime_error("PT viewport filtered linear export is missing");
        for(auto suffix:{".png",".pfm",".json","-albedo.png","-albedo.pfm","-normal.png","-normal.pfm","-samples.png","-denoised.png","-denoised.pfm"})std::filesystem::remove(exportedPrefix+suffix);
    }
    if(baked.radiance.size()!=size_t(baked.width)*baked.height || baked.sunRadius<=0 || glm::length(baked.sunIrradiance)<=0)throw std::runtime_error("PT: render-thread sky bake is empty");
    double energy=0;for(auto pixel:baked.radiance){if(!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(pixel.z) || glm::any(glm::lessThan(pixel,glm::vec3(0))))throw std::runtime_error("PT: baked HDR sky has invalid radiance");energy+=pixel.x+pixel.y+pixel.z;}
    if(energy<=0)throw std::runtime_error("PT: baked sky has no energy");
    Environment environment(baked.width,baked.height,std::move(baked.radiance));Random random(7);
    for(int i=0;i<1000;++i){auto sample=environment.sample(random);if(sample.pdf<=0 || !std::isfinite(sample.pdf))throw std::runtime_error("PT: baked environment sampling PDF is invalid");}
    validateGpuPathTracing(device);
    frozen.draws.erase(frozen.draws.begin()+2); // Replace studio emitter with constant HDR for energy comparison.
    CpuScene waterScene(frozen);waterScene.environment=std::make_shared<Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));
    Options options;options.width=32;options.height=24;options.samples=512;options.maxDepth=6;options.adaptive=false;auto cpu=render(waterScene,options),gpu=renderGpu(waterScene,options,device);double error=0,reference=0;for(size_t i=0;i<cpu.radiance.size();++i){error+=glm::length(cpu.radiance[i]-gpu.radiance[i]);reference+=glm::length(cpu.radiance[i]);}if(error/std::max(reference,1e-9)>.08)throw std::runtime_error("PT: CPU/GPU FFT water/absorption energy differs");
    frozen.frame.cameraPosition={0,.2f,0};glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;frozen.frame.viewProjection=depth*glm::perspective(glm::radians(50.f),32.f/24,.1f,100.f)*glm::lookAt(frozen.frame.cameraPosition,glm::vec3(0,-1,-.01f),glm::vec3(0,0,-1));
    CpuScene submerged(frozen);submerged.environment=waterScene.environment;auto underCpu=render(submerged,options),underGpu=renderGpu(submerged,options,device);double underError=0,underEnergy=0;for(size_t i=0;i<underCpu.radiance.size();++i){underError+=glm::length(underCpu.radiance[i]-underGpu.radiance[i]);underEnergy+=glm::length(underCpu.radiance[i]);}if(underError/std::max(underEnergy,1e-9)>.08)throw std::runtime_error("PT: CPU/GPU underwater primary medium differs");
    {
        auto cameraOptions=options;cameraOptions.samples=8;GpuRenderSession cameraSession(waterScene,cameraOptions,device);cameraSession.step();
        waterScene.updateCamera(frozen.frame);cameraSession.updateCamera(waterScene,32.f/24);
        if(waterScene.cameraData().media!=submerged.cameraData().media||waterScene.cameraData().winding!=submerged.cameraData().winding)throw std::runtime_error("PT: camera-only update retained air medium underwater");
        while(cameraSession.step()){}if(cameraSession.readback().radiance!=renderGpu(submerged,cameraOptions,device).radiance)throw std::runtime_error("PT: camera-only FFT underwater film differs from fresh scene");
    }
    // Smooth sheets: colored NEE visibility and unchanged medium identity.
    auto sheetRoom=makeCausticsScene(32,24,false).snapshot;auto pane=sheetRoom.draws[0];pane.model=glm::translate(glm::mat4(1),glm::vec3(0,1.5f,0));pane.pathTracingKind=5;pane.pathTracingIor=1.5f;pane.parameters.albedoAlpha={.8f,.9f,1,.85f};sheetRoom.draws.push_back(pane);
    auto compareSheet=[&](const CpuScene &scene,const char *label){auto a=render(scene,options),b=renderGpu(scene,options,device);double error=0,energy=0;for(size_t i=0;i<a.radiance.size();++i){error+=glm::length(a.radiance[i]-b.radiance[i]);energy+=glm::length(a.radiance[i]);}if(a.nonFiniteSamples||b.nonFiniteSamples||energy<=0||error/energy>.025)throw std::runtime_error(std::string("PT: CPU/GPU thin sheet mismatch: ")+label);std::cout<<label<<" CPU/GPU relative L1 "<<error/energy<<'\n';};
    options.samples=512;options.maxDepth=16;CpuScene sheets(sheetRoom);if(!sheets.media().empty())throw std::runtime_error("PT: sheet became a volume");compareSheet(sheets,"Tinted thin sheet / area NEE");
    for(auto model:{1u,2u}){auto controlled=sheetRoom;for(auto &draw:controlled.draws)draw.pathTracingBsdfModel=model;CpuScene common(controlled);compareSheet(common,model==1?"Controlled Lambert / thin sheet":"Controlled additive GGX / thin sheet");}
    sheetRoom.draws.erase(sheetRoom.draws.begin()+1,sheetRoom.draws.begin()+3);sheetRoom.draws[0].parameters.albedoAlpha=glm::vec4(1);sheetRoom.draws[0].parameters.factors.y=.5f;sheetRoom.draws[1].parameters.albedoAlpha=glm::vec4(1);CpuScene whiteSheet(sheetRoom);whiteSheet.environment=waterScene.environment;compareSheet(whiteSheet,"Thin sheet / environment MIS");
    // Bounded pool water uses tangent normals and linear textured roughness, unlike FFT world normals.
    auto poolRoom=makeCausticsScene(32,24,false).snapshot;poolRoom.draws.erase(poolRoom.draws.begin()+2);
    auto pool=makeMediumValidationScene(32,24,true).snapshot.draws[0];pool.pathTracingIor=1.333f;pool.pathTracingAbsorption={.12f,.035f,.015f};pool.pathTracingScattering=glm::vec3(0);pool.pathTracingRoughness=.1f;pool.pathTracingRoughnessTexture=true;
    pool.model=glm::translate(glm::mat4(1),glm::vec3(0,.4f,0))*glm::scale(glm::mat4(1),glm::vec3(2,.3f,1.5f));
    auto maps=std::make_shared<render::MaterialPayload>();maps->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{145,118,253,255}});maps->images[3]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{255,40,0,255,255,85,0,255}});pool.material=maps;pool.parameters.emissiveNormal.w=1;poolRoom.draws.push_back(pool);
    CpuScene poolAir(poolRoom);poolAir.environment=waterScene.environment;compareSheet(poolAir,"Bounded pool / tangent normal / roughness atlas");
    for(auto &draw:poolRoom.draws)draw.pathTracingBsdfModel=2;CpuScene commonPool(poolRoom);commonPool.environment=waterScene.environment;compareSheet(commonPool,"Controlled correlated GGX / bounded pool");
    poolRoom.frame.cameraPosition={0,.4f,0};poolRoom.frame.viewProjection=depth*glm::perspective(glm::radians(50.f),32.f/24,.1f,100.f)*glm::lookAt(poolRoom.frame.cameraPosition,glm::vec3(0,2,4),glm::vec3(0,1,0));
    CpuScene poolInside(poolRoom);poolInside.environment=waterScene.environment;compareSheet(poolInside,"Inside bounded pool / mapped exit boundary");
    auto compareSurface=[&](const CpuScene &scene,const char *label){auto a=render(scene,options),b=renderGpu(scene,options,device);double error=0,energy=0;for(size_t i=0;i<a.radiance.size();++i){error+=glm::length(a.radiance[i]-b.radiance[i]);energy+=glm::length(a.radiance[i]);}if(a.nonFiniteSamples||b.nonFiniteSamples||energy<=0||error/energy>.025)throw std::runtime_error(std::string("PT: CPU/GPU shoreline/solar mismatch: ")+label);std::cout<<label<<" CPU/GPU relative L1 "<<error/energy<<'\n';};
    options.samples=512;options.maxDepth=16;
    // Sand detail is sampled independently of the landscape capture texture budget.
    auto shoreRoom=makeCausticsScene(32,24,false).snapshot;shoreRoom.draws.erase(shoreRoom.draws.begin()+1);
    auto &shore=shoreRoom.draws[0];shore.pathTracingKind=1;shore.extension.shoreHeight={0,10,1,1};shore.extension.shoreSurface={.3f,.7f,.95f,1};
    shore.pathTracingShoreline[0]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,{200,60,20,255,40,100,220,255,40,100,220,255,200,60,20,255}});
    shore.pathTracingShoreline[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{160,110,250,255,105,155,250,255}});
    shore.pathTracingShoreline[2]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{255,120,0,255,255,210,0,255}});
    shore.pathTracingShoreline[3]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,{255,255,255,255,100,100,100,255,255,255,255,255,100,100,100,255}});
    CpuScene shoreScene(shoreRoom);shoreScene.environment=waterScene.environment;compareSurface(shoreScene,"World-space sand / normals / roughness / wetness");
    shore.extension.features={int(render::MaterialFeature::RainWetness),65535,0,0};
    CpuScene rainShore(shoreRoom);rainShore.environment=waterScene.environment;compareSurface(rainShore,"Frozen rain / coastal wetness maximum");
    shore.extension.features.x|=int(render::MaterialFeature::SpatialRainWetness);
    for(size_t cell=0;cell<256;++cell)render::setRainWetCell(shore.extension,cell,uint8_t((cell%16)*17));
    CpuScene spatialShore(shoreRoom);spatialShore.environment=waterScene.environment;compareSurface(spatialShore,"Frozen spatial rain / coastal wetness maximum");
    CpuScene solarWater(frozen);solarWater.environment=std::make_shared<Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(.05f)));solarWater.sunDirection=glm::normalize(glm::vec3(.1f,1,.05f));solarWater.sunIrradiance=glm::vec3(2);solarWater.sunRadius=.03f;
    options.samples=1024;options.maxDepth=16;compareSurface(solarWater,"Refracted solar continuation / full mixture PDF");
    // Exercise spectral free flight, phase NEE and boundary transitions on both kernels.
    for(auto &draw:frozen.draws)if(draw.pathTracingKind==3){draw.pathTracingScattering={.12f,.25f,.4f};draw.pathTracingAnisotropy=.65f;}
    options.maxDepth=64;options.samples=1024;
    auto volumeCompare=[&](const CpuScene &scene,const char *label){auto a=render(scene,options),b=renderGpu(scene,options,device);double error=0,energy=0;for(size_t i=0;i<a.radiance.size();++i){error+=glm::length(a.radiance[i]-b.radiance[i]);energy+=glm::length(a.radiance[i]);}if(!a.volumeEvents||!b.volumeEvents||energy<=0||error/energy>.08)throw std::runtime_error(std::string("PT: CPU/GPU volume mismatch: ")+label);std::cout<<label<<" CPU/GPU relative L1 "<<error/energy<<", volume events "<<a.volumeEvents<<" / "<<b.volumeEvents<<'\n';};
    CpuScene scatteringWater(frozen);scatteringWater.environment=waterScene.environment;volumeCompare(scatteringWater,"Submerged FFT water scattering");
    CpuScene solarScatteringWater(frozen);solarScatteringWater.environment=solarWater.environment;solarScatteringWater.sunDirection=solarWater.sunDirection;solarScatteringWater.sunIrradiance=solarWater.sunIrradiance;solarScatteringWater.sunRadius=solarWater.sunRadius;volumeCompare(solarScatteringWater,"Refracted solar / HG phase mixture PDF");
    auto mediumFixture=makeMediumValidationScene(32,24);CpuScene jadeWater(mediumFixture.snapshot);jadeWater.environment=waterScene.environment;volumeCompare(jadeWater,"Nested water/jade random walk");
    for(auto &draw:mediumFixture.snapshot.draws)if(draw.pathTracingIor>0)draw.pathTracingRoughness=draw.pathTracingKind==3?.18f:.32f;
    CpuScene roughJadeWater(mediumFixture.snapshot);roughJadeWater.environment=waterScene.environment;volumeCompare(roughJadeWater,"Rough nested water/jade NEE");
    mediumFixture.snapshot.frame.cameraPosition={0,.2f,0};mediumFixture.snapshot.frame.viewProjection=depth*glm::perspective(glm::radians(50.f),32.f/24,.1f,100.f)*glm::lookAt(mediumFixture.snapshot.frame.cameraPosition,glm::vec3(0,.2f,4),glm::vec3(0,1,0));CpuScene insideJade(mediumFixture.snapshot);insideJade.environment=waterScene.environment;volumeCompare(insideJade,"Camera inside water/jade");
    Options unsupported=options;unsupported.guiding=true;bool learningRejected=false;try{renderGpu(jadeWater,unsupported,device);}catch(const std::invalid_argument &){learningRejected=true;}if(!learningRejected)throw std::runtime_error("PT: volume guiding was not rejected");
    // A small volume enables the thin-boundary ray epsilon; area-light visibility
    // must still trim both endpoints at their world-coordinate precision.
    auto originRoom=makeCausticsScene(32,24,false).snapshot;auto distantMedium=makeMediumValidationScene(32,24,true).snapshot.draws[0];distantMedium.model=glm::translate(glm::mat4(1),glm::vec3(20,0,0));originRoom.draws.push_back(distantMedium);
    CpuScene originScene(originRoom);options.samples=512;options.maxDepth=4;auto originImage=render(originScene,options);auto largeRoom=originRoom;glm::vec3 translation{10000,0,0};largeRoom.frame.cameraPosition+=translation;largeRoom.frame.viewProjection=largeRoom.frame.viewProjection*glm::translate(glm::mat4(1),-translation);for(auto &draw:largeRoom.draws)draw.model=glm::translate(glm::mat4(1),translation)*draw.model;CpuScene largeScene(largeRoom);auto largeCpu=render(largeScene,options),largeGpu=renderGpu(largeScene,options,device);double originEnergy=0,largeEnergy=0,largeGpuEnergy=0,largeError=0;for(size_t i=0;i<largeCpu.radiance.size();++i){originEnergy+=glm::length(originImage.radiance[i]);largeEnergy+=glm::length(largeCpu.radiance[i]);largeGpuEnergy+=glm::length(largeGpu.radiance[i]);largeError+=glm::length(largeCpu.radiance[i]-largeGpu.radiance[i]);}if(originEnergy<=0||std::abs(largeEnergy/originEnergy-1)>.05||largeError/largeEnergy>.08)throw std::runtime_error("PT: scaled area-light shadow segment self-occludes");std::cout<<"Area emitter +10km energy ratio "<<largeEnergy/originEnergy<<", GPU/CPU "<<largeGpuEnergy/largeEnergy<<", relative L1 "<<largeError/largeEnergy<<'\n';
    std::cout<<"Underwater CPU/GPU relative L1 "<<underError/underEnergy<<'\n';
    std::cout<<"Native FFT PT capture motion "<<motion<<", CPU/GPU water relative L1 "<<error/reference<<'\n';
    std::cout<<"CPU PT render-thread HDR sky bake and importance sampling passed\n";
}
} // namespace pt
