#include "engine/RuntimeTrace.h"
#include "engine/AssetPath.h"
#include "engine/FixedStepClock.h"
#include "renderer/rhi/EnvironmentBridge.h"
#include "renderer/rhi/GpuAirFog.h"
#include "renderer/rhi/GpuDdgi.h"
#include "renderer/rhi/DdgiPresets.h"
#include "renderer/rhi/GpuRain.h"
#include<glad/glad.h>
#include "rhi/Device.h"
#include "rhi/GraphicsDevice.h"
#include "rhi/Validation.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/GpuGeometryCuller.h"
#include "renderer/rhi/GpuHiZCuller.h"
#include "renderer/rhi/GpuOcean.h"
#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/GpuVegetation.h"
#include "renderer/rhi/GpuWaveParticles.h"
#include "renderer/rhi/GpuClouds.h"
#include "renderer/rhi/GpuSubdivision.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/rhi/FeatureScenes.h"
#include "renderer/rhi/ClusterFlowCapture.h"
#include "renderer/rhi/AoReference.h"
#ifdef SCENERENDERER_LEGACY_METAL
#include "metal/MetalBackend.h"
#include "metal/MetalDemo.h"
#endif
#include<iostream>
#include<fstream>
#include<chrono>
#include<glfw/glfw3.h>
#include<glm/glm.hpp>
#include<thread>
#include "engine/RenderRuntime.h"
#include "PT/ViewportBenchmark.h"
#include "rhi/MemorySafety.h"
//utils
#include"utils/Utils.h"
#include"utils/Camera.h"
//components
#include"component/GameObject.h"
#include"component/transform.h"
#include"component/Lights.h"
#include"component/Model.h"
//object
#include"object/SkyBox.h"
#include"object/Terrain.h"
//renderer
#include"renderer/Material.h"
#include"system/ResourceManager.h"
#include"component/Mesh_Filter.h"
#include"component/Mesh_Renderer.h"
#include"renderer/RenderScene.h"
//system
#include"system/InputManager.h"
#include"system/meta_register.h"
#include"system/RenderManager.h"
#include"GUI.h"
#include"component/Atmosphere.h"
#include "component/Cloud.h"
#include"system/Loader.h"
#include"PT/PathTracing.h"
#include"system/Config.h"
#include"PT/PTScene.h"
#include"PT/Connector.h"
#include "PT/CpuPathTracer.h"
#include "PT/SceneIdentity.h"
//json
#include<json/json.hpp>
using json = nlohmann::json;
// yaml
#include<yaml-cpp/yaml.h>

#if defined(_WIN32)
extern "C" __declspec(dllexport) long long NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;
#endif

const unsigned int  SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 900;

// manager
//#define TEST
//#ifndef TEST

shared_ptr<RenderScene> scene;
int frameLimit=0;
std::string nativeScreenshot;
float fixedNativeTime=-1;
std::array<int,2> scriptedResize{};
bool startForward=false,startDdgi=false;
int startDdgiProbes=0,startDdgiProbeLayer=-1;bool startDdgiProbeXray=false;
bool singleThreadedNative=false;
int maxFramesInFlight=1;
size_t gpuResourceBudget=0;
bool hiddenEditor=false,automaticQuality=false,startPathTracingViewport=false;
std::array<int,2> windowSize{1600,900};
bool startEnvironment=false,environmentEnableClouds=false,environmentHideRain=false,environmentUniformGround=false,environmentNoCloudShadows=false,environmentNoFog=false,environmentNoAerial=false;
std::optional<double> environmentInitialCoverage,environmentInitialRain;
std::string editorEnvironmentSceneKey,environmentLoadPath,environmentSavePath;
std::optional<uint64_t> environmentInitialTicks;
std::optional<engine::EnvironmentSolar> environmentInitialSolar;
std::optional<engine::EnvironmentWind> environmentInitialWind;

void NativeRealTimeRun(GLFWwindow* window,shared_ptr<RenderScene>& scene){
    Gui gui(window);
    if(environmentNoCloudShadows)RenderManager::GetInstance()->setting.weather.cloudShadows=false;
    if(environmentNoFog)RenderManager::GetInstance()->setting.weather.fog=false;
    if(environmentNoAerial)RenderManager::GetInstance()->setting.weather.aerialPerspective=false;
    engine::Environment environment({editorEnvironmentSceneKey});
    gui.environmentRainVisible_=!environmentHideRain;gui.environment_=&environment;gui.environmentSceneKey_=editorEnvironmentSceneKey;
    bool environmentStartup=true;
    render::SceneSnapshotBuilder snapshots;
    const auto started=std::chrono::steady_clock::now();
    engine::RenderRuntime runtime(rhi::graphicsDevice(),std::move(gui.nativeRenderer_),2,startPathTracingViewport);
    std::shared_ptr<engine::PathTracingViewportRequest> ptViewport;
    std::shared_ptr<engine::PathTracingViewportExport> ptExportTicket;
    std::future<std::string> ptExportFuture;
    uint64_t ptEpoch=0;float ptFrozenZoom=0,ptLastZoom=0;
    glm::mat4 ptLastView{1};pt::SceneIdentity ptIdentity;
    auto ptLastMotion=std::chrono::steady_clock::now();
    gui.ptViewportStart_=startPathTracingViewport;
    auto cancelPt=[&] {if(ptViewport)ptViewport->cancelled.store(true);ptViewport.reset();gui.ptViewportActive_=false;gui.ptViewportPaused_=false;};
    gui.prepareScene_=[&](std::shared_ptr<const render::RenderWorldSnapshot> payload,std::shared_ptr<std::atomic<bool>> cancelled){
        auto target=std::make_shared<render::RenderWorldSnapshot>(*payload);auto& frame=target->frame;
        int width,height;glfwGetFramebufferSize(window,&width,&height);frame.viewportWidth=std::max(1,width);frame.viewportHeight=std::max(1,height);
        if(auto camera=scene->mainCamera()) {frame.view=camera->GetViewMatrix();glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;frame.viewProjection=depth*camera->GetPerspective()*frame.view;
            frame.cameraPosition=camera->getPosition();frame.nearPlane=camera->getNear();frame.farPlane=camera->getFar();target->exposure=camera->getExposure();}
        const auto settings=RenderManager::GetInstance()->setting;
        frame.shadows=settings.enableShadow;frame.ssao=settings.enableSSAO;frame.rsm=settings.enableRSM;frame.taa=settings.enableTSAA;frame.taaHighQuality=settings.taaHighQuality;frame.clusters=settings.clusters;
        target->automaticQuality=settings.automaticQuality;
        frame.forwardShading=!settings.useDefer;frame.ddgi=settings.ddgi;frame.rsmSettings=settings.rsmSettings;frame.shadowSettings=settings.shadowSettings;frame.aoRadius=settings.aoRadius;frame.aoBias=settings.aoBias;frame.aoPower=settings.aoPower;frame.aoHorizon=settings.aoHorizon;frame.aoDenoise=settings.aoDenoise;frame.aoSlices=settings.aoSlices;frame.aoSteps=settings.aoSteps;
        return runtime.prepareScene(std::move(target),std::move(cancelled));
    };
    gui.activateScene_=[&](uint64_t token){cancelPt();runtime.activatePrepared(token);};
    gui.nativeRuntimeStats_=true;auto fpsSampleAt=std::chrono::steady_clock::now();uint64_t fpsSampleFrames=0;
    engine::FixedStepClock logicClock;InputManager::GetInstance()->tick();
    int framesSubmitted=0;
    try{
        while(!glfwWindowShouldClose(window)){
            engine::TraceScope tickTrace("engine/tick","main");
            runtime.rethrowFailure();glfwPollEvents();const auto inputSampledAt=std::chrono::steady_clock::now();scene->applyCommands();
            if(framesSubmitted==4 && scriptedResize[0]>0){glfwSetWindowSize(window,scriptedResize[0],scriptedResize[1]);scriptedResize={};glfwPollEvents();}
            int width,height;glfwGetFramebufferSize(window,&width,&height);
            runtime.notifySurfaceExtent(uint32_t(std::max(0,width)),uint32_t(std::max(0,height)));
            if(width<=0 || height<=0) {
                auto input=InputManager::GetInstance();input->tick();const auto message=input->capture();
                logicClock.setPaused(gui.pauseSimulation_||bool(ptViewport));logicClock.setSpeed(gui.simulationSpeed_);
                logicClock.advance(std::max(0.f,input->deltaFrame),[&](double step){if(gui.environmentReady_)environment.step();if(scene->mainCamera())scene->mainCamera()->applyInput(message,float(step),false);});
                input->reset();glfwWaitEventsTimeout(.016);continue;
            }
            framebuffer_size_callback(window,width,height);
            const auto fpsNow=std::chrono::steady_clock::now();const double fpsSeconds=std::chrono::duration<double>(fpsNow-fpsSampleAt).count();
            if(fpsSeconds>=.5){const auto rendered=runtime.framesRendered();gui.nativeRenderFps_=float(double(rendered-fpsSampleFrames)/fpsSeconds);fpsSampleFrames=rendered;fpsSampleAt=fpsNow;}
            gui.ptViewportStatus_=runtime.pathTracingStatus();gui.simulationSeconds_=RenderManager::GetInstance()->setting.timeOverride>=0?RenderManager::GetInstance()->setting.timeOverride:float(logicClock.seconds());if(gui.environmentReady_&&environment.snapshot().enabled)gui.simulationSeconds_=float(environment.snapshot().seconds);
            {engine::TraceScope trace("engine/ui","main");gui.window(scene);}
            InputManager::GetInstance()->tick();
            if(ptViewport&&!gui.ptViewportActive_)cancelPt();
            if(ptExportFuture.valid()&&ptExportFuture.wait_for(std::chrono::seconds(0))==std::future_status::ready){
                try{gui.ptViewportExportMessage_="Exported: "+ptExportFuture.get();}catch(const std::exception &error){gui.ptViewportExportMessage_=error.what();}
                gui.ptViewportExportPending_=false;ptExportTicket.reset();
            }
            if(ptViewport){ptViewport->paused.store(gui.ptViewportPaused_);ptViewport->denoise.store(gui.ptViewportDenoise_);}
            if(gui.ptViewportExport_){
                gui.ptViewportExport_=false;
                if(ptViewport&&!ptExportFuture.valid()){
                    ptExportTicket=std::make_shared<engine::PathTracingViewportExport>();ptExportTicket->epoch=ptViewport->epoch;
                    const auto stamp=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    ptExportTicket->prefix="build/path-tracing/viewport/epoch-"+std::to_string(ptViewport->epoch)+"-"+std::to_string(stamp);
                    ptExportFuture=ptExportTicket->completion->get_future();gui.ptViewportExportPending_=true;gui.ptViewportExportMessage_.clear();
                }
            }
            if(InputManager::GetInstance()->keyStatus[KEY_R]==PRESSED){gui.ptViewportStart_=true;InputManager::GetInstance()->keyStatus[KEY_R]=RELEASED;}
            logicClock.setPaused(gui.pauseSimulation_||bool(ptViewport));logicClock.setSpeed(gui.simulationSpeed_);
            if(scene->mainCamera()){scene->mainCamera()->setAspect(float(width)/height);scene->mainCamera()->applyInput(InputManager::GetInstance()->capture(),0,true);}
            const auto inputMessage=InputManager::GetInstance()->capture();
            if(ptViewport&&scene->mainCamera())scene->mainCamera()->applyInput(inputMessage,std::min(.05f,std::max(0.f,InputManager::GetInstance()->deltaFrame)),false);
            if(gui.environmentSingleStep_){
                gui.environmentSingleStep_=false;
                if(gui.environmentReady_&&gui.pauseSimulation_&&!ptViewport)environment.step();
            }
            logicClock.advance(std::max(0.f,InputManager::GetInstance()->deltaFrame),[&](double step){if(gui.environmentReady_)environment.step();if(scene->mainCamera())scene->mainCamera()->applyInput(inputMessage,float(step),false);});
            const auto settings=RenderManager::GetInstance()->setting;
            auto captured=[&]{engine::TraceScope trace("engine/snapshot","main");return snapshots.capture(scene,settings.timeOverride>=0?settings.timeOverride:float(logicClock.seconds()),uint32_t(width),uint32_t(height),false);}();
            if(captured){
                auto snapshot=std::make_shared<render::RenderWorldSnapshot>(*captured);auto& frame=snapshot->frame;
                frame.shadows=settings.enableShadow;frame.ssao=settings.enableSSAO;frame.rsm=settings.enableRSM;frame.taa=settings.enableTSAA;frame.taaHighQuality=settings.taaHighQuality;frame.clusters=settings.clusters;frame.aoRadius=settings.aoRadius;frame.aoBias=settings.aoBias;frame.aoPower=settings.aoPower;frame.aoHorizon=settings.aoHorizon;frame.aoDenoise=settings.aoDenoise;frame.aoSlices=settings.aoSlices;frame.aoSteps=settings.aoSteps;frame.toneMapping=settings.enableHDR;frame.postProcess=settings.postProcess;frame.ddgi=settings.ddgi;frame.rsmSettings=settings.rsmSettings;frame.shadowSettings=settings.shadowSettings;frame.directionalEnabled=settings.enableDirectional;frame.forwardShading=!settings.useDefer;
                snapshot->automaticQuality=settings.automaticQuality;
                if(!gui.environmentReady_){
                    if(environmentStartup&&environmentEnableClouds){
                        if(!scene->sky()||!scene->sky()->getComponent<Cloud>())throw std::invalid_argument("Environment clouds require a scene Cloud component");
                        auto cloud=scene->sky()->getComponent<Cloud>();auto cloudSettings=cloud->settings();cloudSettings.enabled=true;
                        cloud->setSettings(cloudSettings);frame.clouds=cloudSettings;
                    }
                    auto environmentConfig=render::environmentConfig(*snapshot,gui.environmentSceneKey_);
                    if(environmentStartup&&startEnvironment)environmentConfig.initialSeconds=0;
                    if(environmentUniformGround)environmentConfig.landGrid.reset();
                    environment.reset(std::move(environmentConfig));
                    gui.environmentReady_=true;gui.environmentSingleStep_=false;gui.environmentMessage_.clear();
                    if(environmentStartup){
                        environmentStartup=false;
                        if(!environmentLoadPath.empty())environment.load(environmentLoadPath);
                        if(startEnvironment)environment.setEnabled(true);
                        if(environmentInitialSolar)environment.enqueueSolar(*environmentInitialSolar,environment.snapshot().tick+1);
                        if(environmentInitialWind)environment.enqueueWind(*environmentInitialWind,environment.snapshot().tick+1);
                        if(environmentInitialCoverage)environment.enqueueCoverage(*environmentInitialCoverage,environment.snapshot().tick+1);
                        if(environmentInitialRain)environment.enqueueRain(*environmentInitialRain,environment.snapshot().tick+1);
                        if(environmentInitialTicks){
                            for(uint64_t i=0;i<*environmentInitialTicks;++i)environment.step();
                            gui.pauseSimulation_=true;
                        }else if(environmentInitialSolar||environmentInitialWind||environmentInitialCoverage||environmentInitialRain)environment.step();
                    }
                }
                snapshot->frame.weather=settings.weather;
                render::applyEnvironment(*snapshot,environment.snapshot());snapshot->frame.rainVisible=gui.environmentRainVisible_;
                const float zoom=scene->mainCamera()?scene->mainCamera()->getZoom():0;
                const auto now=std::chrono::steady_clock::now();
                bool navigation=false,automaticRestart=false;
                if(ptViewport){
                    if(frame.view!=ptLastView||zoom!=ptLastZoom){ptLastMotion=now;ptLastView=frame.view;ptLastZoom=zoom;}
                    navigation=frame.view!=ptViewport->world->frame.view||zoom!=ptFrozenZoom;
                }
                const bool settled=now-ptLastMotion>=std::chrono::milliseconds(250);
                // Content changes during navigation are evaluated when the camera
                // settles; Raster-only frames do not walk PT content tokens.
                pt::SceneIdentity identity;
                if(gui.ptViewportStart_||(ptViewport&&(!navigation||settled)))identity=pt::sceneIdentity(*snapshot);
                automaticRestart=ptViewport&&(!navigation||settled)&&(!(identity==ptIdentity)||navigation);
                if(gui.ptViewportStart_||automaticRestart) {
                    const bool manual=gui.ptViewportStart_;
                    const bool paused=!manual&&gui.ptViewportPaused_;
                    auto prior=ptViewport;if(prior)prior->cancelled.store(true);
                    ptViewport=std::make_shared<engine::PathTracingViewportRequest>();ptViewport->epoch=++ptEpoch;ptViewport->world=snapshot;ptViewport->forceRecapture=manual;
                    auto &options=ptViewport->options;
                    if(prior&&!manual)options=prior->options;
                    options.width=std::max(1u,uint32_t(width)/2);options.height=std::max(1u,uint32_t(height)/2);
                    if(manual){options.samples=uint32_t(gui.ptViewportSamples_);options.maxDepth=uint32_t(gui.ptViewportDepth_);options.adaptive=false;options.gpuBatchSamples=1;options.worldLod=gui.ptWorldLod_;}
                    options.exposure=snapshot->exposure;
                    ptIdentity=identity;ptFrozenZoom=ptLastZoom=zoom;ptLastView=frame.view;
                    gui.ptViewportActive_=true;gui.ptViewportStart_=false;gui.ptViewportPaused_=paused;ptViewport->paused.store(paused);ptViewport->denoise.store(gui.ptViewportDenoise_);navigation=false;
                }

                engine::RenderPacket packet;packet.sampledAt=inputSampledAt;packet.world=std::move(snapshot);packet.pathTracingViewport=ptViewport;packet.pathTracingExport=ptExportTicket;packet.pathTracingNavigation=navigation;packet.gui=render::GuiFrame::capture(ImGui::GetDrawData());
                if(!nativeScreenshot.empty() && frameLimit==1)packet.screenshot=nativeScreenshot;
                if(runtime.trySubmitFrame(std::move(packet))) {
                    ++framesSubmitted;
                    if(frameLimit>0 && --frameLimit==0)glfwSetWindowShouldClose(window,true);
                    if(framesSubmitted%120==0)ResourceManager::GetInstance()->releaseUnused();
                } else glfwWaitEventsTimeout(.002);
            }else {glfwWaitEventsTimeout(.002);}
            InputManager::GetInstance()->reset();
        }
        runtime.finish();
        if(!environmentSavePath.empty())environment.save(environmentSavePath);
    }catch(...){
        auto error=std::current_exception();try{runtime.finish();}catch(...){}
        gui.destroy();RenderManager::GetInstance()->releaseNative();scene->destroy();scene.reset();rhi::shutdown();glfwDestroyWindow(window);glfwTerminate();std::rethrow_exception(error);
    }
    std::cout<<"RHI threaded editor rendered "<<runtime.framesRendered()<<" frames in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" seconds; last render "<<runtime.renderMilliseconds()<<" ms; render CPU p95/p99 "<<runtime.renderP95Milliseconds()<<"/"<<runtime.renderP99Milliseconds()<<" ms; peak queue wait "<<runtime.peakQueueWaitMilliseconds()<<" ms; shared GPU images "<<runtime.imageBytes()/1048576.0<<" MiB / "<<runtime.imageUploads()<<" uploads / "<<runtime.imageCacheHits()<<" hits; peak RHI resource estimate "<<runtime.peakResourceBytes()/1048576.0<<" MiB; memory pressure "<<runtime.memoryPressureEvents()<<" events; rejected publications "<<runtime.rejectedPublications()<<" / fallback frames "<<runtime.fallbackFrames()<<"; chunked mesh uploads "<<runtime.meshUploadBytes()/1048576.0<<" MiB / "<<runtime.meshUploadChunks()<<" chunks / "<<runtime.pendingMeshUploads()<<" pending; pipelines "<<runtime.pipelineBuilds()<<" native builds / "<<runtime.pipelineCacheHits()<<" cache hits; sampled native allocations "<<(runtime.nativeMemorySupported()?std::to_string(runtime.peakNativeBytes()/1048576.0)+" MiB":"unavailable")<<"; peak GPU submission "<<runtime.peakGpuSubmissionMilliseconds()<<" ms; peak input-sample-to-completion acknowledgement "<<runtime.peakCompletionLatencyMilliseconds()<<" ms; quality tier "<<runtime.qualityLevel()<<"; skipped render snapshots "<<runtime.skippedSnapshots()<<"; dropped logic time "<<logicClock.droppedSeconds()<<" s; CPU queue 2, GPU frame limit "<<maxFramesInFlight<<"\n";
    gui.destroy();RenderManager::GetInstance()->releaseNative();scene->destroy();scene.reset();ResourceManager::GetInstance()->releaseUnused();rhi::shutdown();glfwDestroyWindow(window);glfwTerminate();
}

void RealTimeRun(GLFWwindow* window, shared_ptr<RenderScene>& scene) {
    if(rhi::usesNativeRenderer() && !singleThreadedNative){NativeRealTimeRun(window,scene);return;}
	
	
	//gui
	Gui gui(window);

	// model loading
			
	int framesRendered=0;const auto started=std::chrono::steady_clock::now();
	while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        scene->applyCommands();
        if(framesRendered==4 && scriptedResize[0]>0){glfwSetWindowSize(window,scriptedResize[0],scriptedResize[1]);scriptedResize={};glfwPollEvents();}
        int width,height;glfwGetFramebufferSize(window,&width,&height);if(width<=0 || height<=0){glfwWaitEventsTimeout(.05);continue;}
        framebuffer_size_callback(window,width,height);

        rhi::device()->beginFrame();
		gui.window(scene);
		glfwPollEvents();
		//input manager tick
		InputManager::GetInstance()->tick();

		if(InputManager::GetInstance()->keyStatus[KEY_R] == PRESSED){
			//阻塞
			Connector::GetInstance()->LaunchPathTracingWithRenderScene(scene);
			InputManager::GetInstance()->keyStatus[KEY_R] = RELEASED;
		}

		// camera tick
		if (scene->mainCamera()) {
			scene->mainCamera()->tick();
		}

		RenderManager::GetInstance()->render(scene);

        const bool captureScreenshot=!nativeScreenshot.empty() && frameLimit==1 && RenderManager::GetInstance()->native();
        if(!captureScreenshot)gui.render();
        if(captureScreenshot){
            auto pixels=rhi::graphicsDevice()->readTexture(RenderManager::GetInstance()->output());auto input=InputManager::GetInstance();std::ofstream output(nativeScreenshot,std::ios::binary);output<<"P6\n"<<input->width<<" "<<input->height<<"\n255\n";for(size_t i=0;i<pixels.size();i+=4)output.write(reinterpret_cast<const char*>(pixels.data()+i),3);if(!output)throw std::runtime_error("Cannot write editor screenshot");
        }
        if(RenderManager::GetInstance()->native())rhi::graphicsDevice()->copyToBackbuffer(RenderManager::GetInstance()->output());
		InputManager::GetInstance()->reset();
        rhi::device()->present();++framesRendered;
        if(frameLimit>0 && --frameLimit==0)glfwSetWindowShouldClose(window,true);
	}
    rhi::device()->waitIdle();std::cout<<"RHI editor rendered "<<framesRendered<<" frames in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" seconds; frame limit "<<maxFramesInFlight<<"\n";
	//glDeleteBuffers()
	gui.destroy();
    RenderManager::GetInstance()->releaseNative();scene->destroy();scene.reset();
    rhi::shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();
}

int main(int argc, char** argv) {
    // Parse safety limits before every CLI entry point, including offscreen
    // captures and CPU hierarchy tools. Zero selects automatic limits.
    try{
        uint64_t processBudget=0;
        for(int i=1;i<argc;++i){const std::string argument=argv[i];
            if(argument!="--memory-budget-mib"&&argument!="--gpu-resource-budget-mib")continue;
            if(i+1>=argc)throw std::invalid_argument(argument+" requires nonnegative MiB");
            const std::string value=argv[i+1];size_t consumed=0;
            if(value.empty()||value[0]=='-')throw std::invalid_argument("Memory budget expects nonnegative MiB");
            const auto mib=std::stoull(value,&consumed);
            if(consumed!=value.size()||mib>SIZE_MAX/(1024*1024))throw std::invalid_argument("Memory budget exceeds supported byte range");
            if(argument=="--memory-budget-mib")processBudget=mib*1024*1024;else gpuResourceBudget=size_t(mib)*1024*1024;
            for(int k=i;k+2<argc;++k)argv[k]=argv[k+2];argc-=2;argv[argc]=nullptr;--i;
        }
        rhi::memorySafety().configure(processBudget,gpuResourceBudget);
        const auto memory=rhi::memorySafety().stats();
        std::cout<<"Memory safety: process "<<memory.processBudgetBytes/1048576<<" MiB, GPU "<<memory.gpuBudgetBytes/1048576<<" MiB, physical "<<memory.process.physicalBytes/1048576<<" MiB; memory admission enabled\n";
        if(argc>1&&std::string(argv[1])=="--memory-info"){
            std::cout<<"Process footprint "<<memory.process.footprintBytes/1048576<<" MiB ("<<(memory.process.footprintSupported?"supported":"unavailable")<<"), pressure "<<memory.process.pressure<<"\n";return 0;
        }
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    if(argc>1&&std::string(argv[1])=="--cluster-parent-sweep"){try{if(argc!=4)throw std::invalid_argument("Usage: --cluster-parent-sweep SCENE OUTPUT.json");render::inspectClusterParents(argv[2],argv[3]);return 0;}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
    if(argc>1&&std::string(argv[1])=="--cluster-hierarchy-self-test"){try{render::validateClusterHierarchy();std::cout<<"Cluster hierarchy validation passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
    if(argc>1&&std::string(argv[1])=="--pt-world-lod-self-test"){try{pt::validateWorldLod();std::cout<<"PT clustering LOD validation passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
    try {
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--asset-root") {
        if(i+1>=argc)throw std::invalid_argument("--asset-root requires a directory");
        engine::AssetPath::setRoot(argv[i+1]);for(int k=i;k+2<argc;++k)argv[k]=argv[k+2];argc-=2;--i;
    }
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--backend" && i+1<argc){const std::string name=argv[++i];if(name=="Vulkan")rhi::requestBackend(rhi::Backend::Vulkan);else if(name=="Metal")rhi::requestBackend(rhi::Backend::Metal);else if(name=="OpenGL")rhi::requestBackend(rhi::Backend::OpenGL);else throw std::invalid_argument("Unknown backend: "+name);}
#ifndef SCENERENDERER_HAS_VULKAN
    if(rhi::requestedBackend()==rhi::Backend::Vulkan)throw std::invalid_argument("This build excludes Vulkan; enable SCENERENDERER_VULKAN_PROTOTYPE or select the Vulkan backend in CMake");
#endif
#ifdef SCENERENDERER_METAL
    if(rhi::requestedBackend()==rhi::Backend::OpenGL)throw std::invalid_argument("Use an OpenGL CMake build for the legacy OpenGL renderer");
#else
    if(rhi::requestedBackend()==rhi::Backend::Metal)throw std::invalid_argument("Use a Metal CMake build on macOS");
#endif
    if(argc>1 && std::string(argv[1])=="--ao-reference-self-test"){render::validateAoReference();return 0;}
    if(argc>1 && std::string(argv[1])=="--ao-benchmark")return render::runAoBenchmark(argc,argv);
    if (argc > 1 && (std::string(argv[1]) == "--path-trace" || std::string(argv[1]) == "--path-trace-gpu")) return pt::runCommandLine(argc,argv);
    if (argc > 1 && (std::string(argv[1]) == "--rhi-forward" || std::string(argv[1]) == "--rhi-deferred" || std::string(argv[1]) == "--rhi-scene")) { render::runForwardScene(argc,argv);return 0; }
    if (argc > 1 && (std::string(argv[1]) == "--nature-self-test" || std::string(argv[1]) == "--temporal-self-test" || std::string(argv[1]) == "--raster-preview-self-test" || std::string(argv[1]) == "--ddgi-self-test" || std::string(argv[1]) == "--water-self-test" || std::string(argv[1]) == "--ocean-self-test" || std::string(argv[1]) == "--cloud-self-test" || std::string(argv[1]) == "--post-process-self-test" || std::string(argv[1]) == "--cluster-rendering-self-test" || std::string(argv[1]) == "--cluster-scene-flow" || std::string(argv[1]) == "--geometry-culling-self-test" || std::string(argv[1]) == "--hiz-culling-self-test" || std::string(argv[1]) == "--rain-self-test" || std::string(argv[1]) == "--engine-self-test" || std::string(argv[1]) == "--weather-lighting-self-test")) {
        if(rhi::requestedBackend()==rhi::Backend::OpenGL)throw std::invalid_argument("Water validation requires Metal or Vulkan");
#ifdef SCENERENDERER_HAS_VULKAN
        if(rhi::requestedBackend()==rhi::Backend::Vulkan)rhi::configureVulkanWindowing();
#endif
        if(!glfwInit())throw std::runtime_error("Water validation GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);GLFWwindow* window=nullptr;
        try {
            if(createWindow(window,64,64)!=0 || gladInit()!=0)throw std::runtime_error("Water validation device initialization failed");
            if(std::string(argv[1])=="--temporal-self-test")render::validateTemporalRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--raster-preview-self-test")render::validateRasterInstanceBatching(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--engine-self-test")render::validateEngineBasics(rhi::graphicsDevice());
            else if(std::string(argv[1])=="--ddgi-self-test")render::validateDdgi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--weather-lighting-self-test")render::validateWeatherLighting(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--ocean-self-test")render::validateOceanRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--cloud-self-test")render::validateCloudsRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--rain-self-test")render::validateRain(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--hiz-culling-self-test")render::validateHiZCulling(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--cluster-scene-flow"){if(argc<4)throw std::invalid_argument("Usage: --cluster-scene-flow SCENE OUTPUT_DIR [--backend Metal|Vulkan]");render::captureClusterSceneFlow(rhi::graphicsDevice(),rhi::defaultShaderDirectory(),argv[2],argv[3]);}
            else if(std::string(argv[1])=="--cluster-rendering-self-test"){render::validateClusterRendering(rhi::graphicsDevice(),rhi::defaultShaderDirectory());render::captureClusterScene(rhi::graphicsDevice(),rhi::defaultShaderDirectory());}
            else if(std::string(argv[1])=="--geometry-culling-self-test")render::validateGeometryCulling(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--post-process-self-test")render::validatePostProcessing(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            else if(std::string(argv[1])=="--nature-self-test"){render::validateVegetation(rhi::graphicsDevice(),rhi::defaultShaderDirectory());render::validateWaveParticles(rhi::graphicsDevice(),rhi::defaultShaderDirectory());}
            else {render::validateAtmosphereRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());render::validateOceanRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());render::validateTemporalRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());}
            rhi::shutdown();
        }catch(...){try{rhi::shutdown();}catch(...){}if(window)glfwDestroyWindow(window);glfwTerminate();throw;}
        glfwDestroyWindow(window);glfwTerminate();return 0;
    }
    if (argc > 1 && (std::string(argv[1]) == "--pt-self-test" || std::string(argv[1]) == "--pt-viewport-benchmark")) {
        if(rhi::requestedBackend()==rhi::Backend::OpenGL)throw std::invalid_argument("PT sky bridge test requires Metal or Vulkan; CPU tests run separately");
#ifdef SCENERENDERER_HAS_VULKAN
        if(rhi::requestedBackend()==rhi::Backend::Vulkan)rhi::configureVulkanWindowing();
#endif
        if(!glfwInit())throw std::runtime_error("PT validation GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);GLFWwindow* window=nullptr;
        try {
            if(createWindow(window,64,64)!=0 || gladInit()!=0)throw std::runtime_error("PT validation device initialization failed");
            if(std::string(argv[1])=="--pt-viewport-benchmark")pt::runViewportBenchmark(argc,argv,rhi::graphicsDevice(),window);
            else pt::validatePathTracingBridge(rhi::graphicsDevice());
            rhi::shutdown();
        }catch(...){try{rhi::shutdown();}catch(...){}if(window)glfwDestroyWindow(window);glfwTerminate();throw;}
        glfwDestroyWindow(window);glfwTerminate();return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--rhi-self-test") {
#ifdef SCENERENDERER_HAS_VULKAN
        if(rhi::requestedBackend()==rhi::Backend::Vulkan)rhi::configureVulkanWindowing();
#endif
        if (!glfwInit()) throw std::runtime_error("RHI validation: GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        GLFWwindow* validationWindow = nullptr;
        if (createWindow(validationWindow, 64, 64) != 0) return 1;
        try {
            if (gladInit() != 0) throw std::runtime_error("RHI validation: GLAD initialization failed");
            rhi::validateBufferTransfers(*rhi::device(),rhi::device()->backend()==rhi::Backend::OpenGL);
            rhi::validateTexturedRendering(*rhi::graphicsDevice());
            render::validateForwardRendering(rhi::graphicsDevice(), rhi::defaultShaderDirectory());
            render::validateEngineBasics(rhi::graphicsDevice());
            render::validateSceneSolarControls(rhi::graphicsDevice());
            render::validateSceneEffects(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        render::validateAtmosphereRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        render::validateCloudsRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        render::validateTerrainRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        render::validateSubdivisionRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        render::validateTemporalRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        rhi::validateComputeAndIndirect(*rhi::graphicsDevice());
            render::validateOceanRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            rhi::validateFrameLifecycle(rhi::graphicsDevice());
            pt::validatePathTracingBridge(rhi::graphicsDevice());
            rhi::shutdown();
        } catch (...) {
            rhi::shutdown();glfwDestroyWindow(validationWindow);glfwTerminate();throw;
        }
        glfwDestroyWindow(validationWindow);glfwTerminate();
        std::cout << "RHI native GPU buffer uploads and readback passed\n";
        return 0;
    }
#ifdef SCENERENDERER_LEGACY_METAL
    if (argc>1 && std::string(argv[1])=="--metal-self-test") { rhi::useLegacyRenderer(true); MetalBackend::selfTest(); return 0; }
    if (argc>1 && std::string(argv[1])=="--legacy-gallery") { rhi::useLegacyRenderer(true); renderMetalGallery(argc>2?argv[2]:"img/metal", argc>3?argv[3]:""); return 0; }
#endif
    if(argc>1 && std::string(argv[1])=="--render-gallery"){render::runFeatureGallery(argc>2?argv[2]:"build/rhi/gallery",argc>3 && std::string(argv[3])!="--backend"?argv[3]:"core");return 0;}
    bool forceDemo=false;
    std::string classicScene;
    for(int i=1;i<argc;i++) {
        std::string argument=argv[i];
        if(argument=="--demo")forceDemo=true;
        else if(argument=="--backend" && i+1<argc){std::string name=argv[++i];if(name=="Vulkan")rhi::requestBackend(rhi::Backend::Vulkan);else if(name=="Metal")rhi::requestBackend(rhi::Backend::Metal);else if(name=="OpenGL")rhi::requestBackend(rhi::Backend::OpenGL);else throw std::invalid_argument("Unknown backend");}
        else if(argument=="--classic"&&i+1<argc)classicScene=argv[++i];
        else if(argument=="--hidden")hiddenEditor=true;
        else if(argument=="--pt-viewport")startPathTracingViewport=true;
        else if(argument=="--auto-quality")automaticQuality=true;
        else if(argument=="--environment")startEnvironment=true;
        else if(argument=="--environment-clouds"){environmentEnableClouds=true;startEnvironment=true;}
        else if(argument=="--environment-no-cloud-shadows"){environmentNoCloudShadows=true;startEnvironment=true;}
        else if(argument=="--environment-no-fog"){environmentNoFog=true;startEnvironment=true;}
        else if(argument=="--environment-no-aerial-perspective"){environmentNoAerial=true;startEnvironment=true;}
        else if(argument=="--environment-hide-rain")environmentHideRain=true;
        else if(argument=="--environment-uniform-ground")environmentUniformGround=true;
        else if(argument=="--environment-rain"){
            if(i+1>=argc)throw std::invalid_argument("--environment-rain requires mm/h in [0,1000]");
            const std::string text=argv[++i];size_t consumed=0;const double value=std::stod(text,&consumed);
            if(consumed!=text.size()||!std::isfinite(value)||value<0||value>1000)throw std::invalid_argument("Environment rain must be finite and in [0,1000] mm/h");
            environmentInitialRain=value/3600000;startEnvironment=true;
        }
        else if(argument=="--environment-cloud-coverage"){
            if(i+1>=argc)throw std::invalid_argument("--environment-cloud-coverage requires a number in [0,1]");
            const std::string value=argv[++i];size_t consumed=0;double coverage=std::stod(value,&consumed);
            if(consumed!=value.size()||!std::isfinite(coverage)||coverage<0||coverage>1)throw std::invalid_argument("Environment cloud coverage must be finite and in [0,1]");
            environmentInitialCoverage=coverage;startEnvironment=true;
        }
        else if(argument=="--environment-load"||argument=="--environment-save"){
            if(i+1>=argc)throw std::invalid_argument(argument+" requires a checkpoint path");
            (argument=="--environment-load"?environmentLoadPath:environmentSavePath)=argv[++i];startEnvironment=true;
        }
        else if(argument=="--environment-ticks"){
            if(i+1>=argc)throw std::invalid_argument("--environment-ticks requires a positive integer");
            const std::string value=argv[++i];size_t consumed=0;
            if(value.empty()||value[0]=='-')throw std::invalid_argument("Invalid environment tick count");
            auto count=std::stoull(value,&consumed);
            if(consumed!=value.size()||count==0||count>3600)throw std::invalid_argument("Environment preroll supports 1..3600 ticks");
            environmentInitialTicks=count;startEnvironment=true;
        }
        else if(argument=="--environment-wind"){
            if(i+2>=argc)throw std::invalid_argument("--environment-wind expects X Z in m/s");
            auto component=[](const std::string& text){size_t consumed=0;const double value=std::stod(text,&consumed);
                if(consumed!=text.size()||!std::isfinite(value)||std::abs(value)>100)throw std::invalid_argument("Environment wind components must be finite numbers within [-100,100]");return value;};
            const double x=component(argv[++i]),z=component(argv[++i]);environmentInitialWind=engine::EnvironmentWind{x,z};startEnvironment=true;
        }
        else if(argument=="--environment-sun"){
            if(i+2>=argc)throw std::invalid_argument("--environment-sun expects ELEVATION AZIMUTH in degrees");
            auto angle=[](const std::string& text){size_t consumed=0;const auto value=std::stod(text,&consumed);
                if(consumed!=text.size()||!std::isfinite(value))throw std::invalid_argument("Environment sun angles must be finite numbers");return value;};
            const auto elevation=angle(argv[++i]),azimuth=angle(argv[++i]);
            if(elevation < -90 || elevation > 90 || azimuth < -360 || azimuth > 360)
                throw std::invalid_argument("Environment sun angles outside supported range");
            environmentInitialSolar=engine::EnvironmentSolar{elevation,azimuth};startEnvironment=true;
        }
        else if(argument=="--gpu-resource-budget-mib" && i+1<argc){const std::string text=argv[++i];if(text.empty() || text[0]=='-')throw std::invalid_argument("Resource budget expects nonnegative MiB");size_t consumed=0;auto value=std::stoull(text,&consumed);if(consumed!=text.size() || value>SIZE_MAX/(1024*1024))throw std::invalid_argument("Resource budget exceeds supported byte range");gpuResourceBudget=size_t(value)*1024*1024;}
        else if(argument=="--frames-in-flight" && i+1<argc)maxFramesInFlight=std::stoi(argv[++i]);
        else if(argument=="--size" && i+1<argc){const std::string size=argv[++i];auto x=size.find('x');if(x==std::string::npos)throw std::invalid_argument("Size expects WIDTHxHEIGHT");windowSize={std::stoi(size.substr(0,x)),std::stoi(size.substr(x+1))};if(windowSize[0]<=0 || windowSize[1]<=0)throw std::invalid_argument("Window dimensions must be positive");}
        else if(argument=="--forward")startForward=true;
        else if(argument=="--ddgi")startDdgi=true;
        else if(argument=="--ddgi-probes"&&i+1<argc){
            const std::string mode=argv[++i];
            if(mode!="irradiance"&&mode!="state")throw std::invalid_argument("--ddgi-probes expects irradiance or state");
            startDdgi=true;startDdgiProbes=mode=="state"?2:1;
        }
        else if(argument=="--ddgi-probe-xray")startDdgiProbeXray=true;
        else if(argument=="--ddgi-probe-layer"&&i+1<argc)startDdgiProbeLayer=std::stoi(argv[++i]);
        else if(argument=="--single-thread")singleThreadedNative=true;
        else if(argument=="--resize" && i+1<argc){const std::string size=argv[++i];auto x=size.find('x');if(x==std::string::npos)throw std::invalid_argument("Resize expects WIDTHxHEIGHT");scriptedResize={std::stoi(size.substr(0,x)),std::stoi(size.substr(x+1))};if(scriptedResize[0]<=0 || scriptedResize[1]<=0)throw std::invalid_argument("Resize dimensions must be positive");}
        else if(argument=="--time" && i+1<argc)fixedNativeTime=std::stof(argv[++i]);
        else if(argument=="--screenshot" && i+1<argc)nativeScreenshot=argv[++i];
        else if(argument=="--frames"&&i+1<argc){frameLimit=std::stoi(argv[++i]);if(frameLimit<=0)throw std::invalid_argument("Frames must be positive");}
        else throw std::invalid_argument("Unknown argument: "+argument);
    }
    if(startPathTracingViewport&&(singleThreadedNative||rhi::requestedBackend()==rhi::Backend::OpenGL))
        throw std::invalid_argument("--pt-viewport requires the threaded Metal/Vulkan editor");
    if(startEnvironment&&(singleThreadedNative||rhi::requestedBackend()==rhi::Backend::OpenGL||fixedNativeTime>=0))
        throw std::invalid_argument("Environment requires the threaded Metal/Vulkan editor without --time; use --environment-ticks for a frozen state");
	(void)argc;
	(void)argv;
	// 
	// render();
	//test();

	auto config = Config::GetInstance();
	config->parse("./config.json");

#ifdef SCENERENDERER_HAS_VULKAN
    if(rhi::requestedBackend()==rhi::Backend::Vulkan)rhi::configureVulkanWindowing();
#endif
	glfwInit();
	GLFWwindow* window; 
	if(hiddenEditor)glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
	if(createWindow(window, windowSize[0], windowSize[1])!=0) return 1;
#ifndef SCENERENDERER_METAL
	if(rhi::requestedBackend()==rhi::Backend::OpenGL)glfwMakeContextCurrent(window);
#endif
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    int framebufferWidth,framebufferHeight;
    glfwGetFramebufferSize(window,&framebufferWidth,&framebufferHeight);
    framebuffer_size_callback(window,framebufferWidth,framebufferHeight);
    std::cout<<"RHI editor framebuffer "<<framebufferWidth<<"x"<<framebufferHeight<<" pixels\n";
	
	//glad
	if (gladInit() != 0) return 1;
    rhi::device()->setMaxFramesInFlight(maxFramesInFlight);
    if(gpuResourceBudget && !rhi::usesNativeRenderer())throw std::invalid_argument("Resource budget applies to the native RHI editor, not the legacy GL renderer");
    rhi::device()->setResourceBudget(gpuResourceBudget);
    RenderManager::GetInstance()->setting.automaticQuality=automaticQuality;
	if(!rhi::usesNativeRenderer()){glEnable(GL_DEPTH_TEST);
	//glDepthMask(GL_FALSE);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glEnable(GL_PROGRAM_POINT_SIZE);}

	// scene loading
	scene = std::make_shared<RenderScene>();
	RenderManager::GetInstance()->init();
    RenderManager::GetInstance()->setting.timeOverride=fixedNativeTime;RenderManager::GetInstance()->setting.useDefer=!startForward;
	// Camera
	{
		std::shared_ptr<Camera> camera = std::make_shared<Camera>();
		scene->setCamera(camera);
	}
    if(!classicScene.empty()){scene=render::makeClassicScene(classicScene);editorEnvironmentSceneKey="classic:"+classicScene;}
    else if(forceDemo || !std::filesystem::exists(config->scene_file)) {
        std::cout << "Loading the built-in RHI feature scene\n";
        scene=rhi::usesNativeRenderer()?render::makeFeatureScene():render::makeForwardDemoScene();
        editorEnvironmentSceneKey="builtin:demo";
    } else {Loader::GetInstance()->loadSceneAsync(scene,config->scene_file);editorEnvironmentSceneKey="file:"+std::filesystem::absolute(config->scene_file).lexically_normal().string();}

    RenderManager::GetInstance()->setting.useDefer=!startForward;
    if(startDdgi)RenderManager::GetInstance()->setting.ddgi=render::ddgiScenePreset(classicScene);
    auto& startupGi=RenderManager::GetInstance()->setting.ddgi;
    startupGi.debugProbes=startDdgiProbes;startupGi.debugLayer=startDdgiProbeLayer;startupGi.debugXray=startDdgiProbeXray;
    startupGi.validate();
	if(config->bGui){
		RealTimeRun(window,scene);
	}
	else {
		Connector::GetInstance()->LaunchPathTracingWithRenderScene(scene);
	}
	rhi::shutdown();

	return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << "\n";return 1;}
}
