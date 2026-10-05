#include "engine/AssetPath.h"
#include "engine/FixedStepClock.h"
#include<glad/glad.h>
#include "rhi/Device.h"
#include "rhi/GraphicsDevice.h"
#include "rhi/Validation.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/GpuOcean.h"
#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/GpuClouds.h"
#include "renderer/rhi/GpuSubdivision.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/rhi/FeatureScenes.h"
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
#include"system/Loader.h"
#include"PT/PathTracing.h"
#include"system/Config.h"
#include"PT/PTScene.h"
#include"PT/Connector.h"
#include "PT/CpuPathTracer.h"
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
bool startForward=false;
bool singleThreadedNative=false;
int maxFramesInFlight=3;
size_t gpuResourceBudget=0;
bool hiddenEditor=false,automaticQuality=false;
std::array<int,2> windowSize{1600,900};

void NativeRealTimeRun(GLFWwindow* window,shared_ptr<RenderScene>& scene){
    Gui gui(window);
    render::SceneSnapshotBuilder snapshots;
    const auto started=std::chrono::steady_clock::now();
    engine::RenderRuntime runtime(rhi::graphicsDevice(),std::move(gui.nativeRenderer_));
    gui.prepareScene_=[&](std::shared_ptr<const render::RenderWorldSnapshot> payload,std::shared_ptr<std::atomic<bool>> cancelled){
        auto target=std::make_shared<render::RenderWorldSnapshot>(*payload);auto& frame=target->frame;
        int width,height;glfwGetFramebufferSize(window,&width,&height);frame.viewportWidth=std::max(1,width);frame.viewportHeight=std::max(1,height);
        if(auto camera=scene->mainCamera()) {frame.view=camera->GetViewMatrix();glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;frame.viewProjection=depth*camera->GetPerspective()*frame.view;
            frame.cameraPosition=camera->getPosition();frame.nearPlane=camera->getNear();frame.farPlane=camera->getFar();target->exposure=camera->getExposure();}
        const auto settings=RenderManager::GetInstance()->setting;
        frame.shadows=settings.enableShadow;frame.ssao=settings.enableSSAO;frame.rsm=settings.enableRSM;frame.taa=settings.enableTSAA;
        target->automaticQuality=settings.automaticQuality;
        frame.forwardShading=!settings.useDefer;frame.rsmSettings=settings.rsmSettings;frame.shadowSettings=settings.shadowSettings;
        return runtime.prepareScene(std::move(target),std::move(cancelled));
    };
    gui.activateScene_=[&](uint64_t token){runtime.activatePrepared(token);};
    engine::FixedStepClock logicClock;InputManager::GetInstance()->tick();
    int framesSubmitted=0;
    try{
        while(!glfwWindowShouldClose(window)){
            runtime.rethrowFailure();glfwPollEvents();const auto inputSampledAt=std::chrono::steady_clock::now();scene->applyCommands();
            if(framesSubmitted==4 && scriptedResize[0]>0){glfwSetWindowSize(window,scriptedResize[0],scriptedResize[1]);scriptedResize={};glfwPollEvents();}
            int width,height;glfwGetFramebufferSize(window,&width,&height);
            runtime.notifySurfaceExtent(uint32_t(std::max(0,width)),uint32_t(std::max(0,height)));
            if(width<=0 || height<=0) {
                auto input=InputManager::GetInstance();input->tick();const auto message=input->capture();
                logicClock.setPaused(gui.pauseSimulation_);logicClock.setSpeed(gui.simulationSpeed_);
                logicClock.advance(std::max(0.f,input->deltaFrame),[&](double step){if(scene->mainCamera())scene->mainCamera()->applyInput(message,float(step),false);});
                input->reset();glfwWaitEventsTimeout(.016);continue;
            }
            framebuffer_size_callback(window,width,height);
            gui.window(scene);InputManager::GetInstance()->tick();
            if(InputManager::GetInstance()->keyStatus[KEY_R]==PRESSED){
                auto captured=std::make_shared<render::RenderWorldSnapshot>(*snapshots.capture(scene,RenderManager::GetInstance()->setting.timeOverride>=0?RenderManager::GetInstance()->setting.timeOverride:float(logicClock.seconds()),uint32_t(width),uint32_t(height)));
                captured->frame.directionalEnabled=RenderManager::GetInstance()->setting.enableDirectional;
                auto baked=runtime.captureAtmosphere(captured->frame);
                captured=std::make_shared<render::RenderWorldSnapshot>(runtime.capturePathTracingScene(*captured));
                Connector::GetInstance()->LaunchPathTracingWithSnapshot(std::move(captured),baked);
                InputManager::GetInstance()->keyStatus[KEY_R]=RELEASED;
            }
            logicClock.setPaused(gui.pauseSimulation_);logicClock.setSpeed(gui.simulationSpeed_);
            if(scene->mainCamera()){scene->mainCamera()->setAspect(float(width)/height);scene->mainCamera()->applyInput(InputManager::GetInstance()->capture(),0,true);}
            const auto inputMessage=InputManager::GetInstance()->capture();
            logicClock.advance(std::max(0.f,InputManager::GetInstance()->deltaFrame),[&](double step){if(scene->mainCamera())scene->mainCamera()->applyInput(inputMessage,float(step),false);});
            const auto settings=RenderManager::GetInstance()->setting;
            auto captured=snapshots.capture(scene,settings.timeOverride>=0?settings.timeOverride:float(logicClock.seconds()),uint32_t(width),uint32_t(height),false);
            if(captured){
                auto snapshot=std::make_shared<render::RenderWorldSnapshot>(*captured);auto& frame=snapshot->frame;
                frame.shadows=settings.enableShadow;frame.ssao=settings.enableSSAO;frame.rsm=settings.enableRSM;frame.taa=settings.enableTSAA;frame.aoRadius=settings.aoRadius;frame.aoBias=settings.aoBias;frame.aoPower=settings.aoPower;frame.toneMapping=settings.enableHDR;frame.rsmSettings=settings.rsmSettings;frame.shadowSettings=settings.shadowSettings;frame.directionalEnabled=settings.enableDirectional;frame.forwardShading=!settings.useDefer;
                snapshot->automaticQuality=settings.automaticQuality;
                engine::RenderPacket packet;packet.sampledAt=inputSampledAt;packet.world=std::move(snapshot);packet.gui=render::GuiFrame::capture(ImGui::GetDrawData());
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

		gui.render();
        if(!nativeScreenshot.empty() && frameLimit==1 && RenderManager::GetInstance()->native()){
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
    if (argc > 1 && (std::string(argv[1]) == "--path-trace" || std::string(argv[1]) == "--path-trace-gpu")) return pt::runCommandLine(argc,argv);
    if (argc > 1 && (std::string(argv[1]) == "--rhi-forward" || std::string(argv[1]) == "--rhi-deferred" || std::string(argv[1]) == "--rhi-scene")) { render::runForwardScene(argc,argv);return 0; }
    if (argc > 1 && std::string(argv[1]) == "--pt-self-test") {
        if(rhi::requestedBackend()==rhi::Backend::OpenGL)throw std::invalid_argument("PT sky bridge test requires Metal or Vulkan; CPU tests run separately");
#ifdef SCENERENDERER_HAS_VULKAN
        if(rhi::requestedBackend()==rhi::Backend::Vulkan)rhi::configureVulkanWindowing();
#endif
        if(!glfwInit())throw std::runtime_error("PT validation GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);GLFWwindow* window=nullptr;
        try {
            if(createWindow(window,64,64)!=0 || gladInit()!=0)throw std::runtime_error("PT validation device initialization failed");
            pt::validatePathTracingBridge(rhi::graphicsDevice());rhi::shutdown();
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
        else if(argument=="--auto-quality")automaticQuality=true;
        else if(argument=="--gpu-resource-budget-mib" && i+1<argc){const std::string text=argv[++i];if(text.empty() || text[0]=='-')throw std::invalid_argument("Resource budget expects nonnegative MiB");size_t consumed=0;auto value=std::stoull(text,&consumed);if(consumed!=text.size() || value>SIZE_MAX/(1024*1024))throw std::invalid_argument("Resource budget exceeds supported byte range");gpuResourceBudget=size_t(value)*1024*1024;}
        else if(argument=="--frames-in-flight" && i+1<argc)maxFramesInFlight=std::stoi(argv[++i]);
        else if(argument=="--size" && i+1<argc){const std::string size=argv[++i];auto x=size.find('x');if(x==std::string::npos)throw std::invalid_argument("Size expects WIDTHxHEIGHT");windowSize={std::stoi(size.substr(0,x)),std::stoi(size.substr(x+1))};if(windowSize[0]<=0 || windowSize[1]<=0)throw std::invalid_argument("Window dimensions must be positive");}
        else if(argument=="--forward")startForward=true;
        else if(argument=="--single-thread")singleThreadedNative=true;
        else if(argument=="--resize" && i+1<argc){const std::string size=argv[++i];auto x=size.find('x');if(x==std::string::npos)throw std::invalid_argument("Resize expects WIDTHxHEIGHT");scriptedResize={std::stoi(size.substr(0,x)),std::stoi(size.substr(x+1))};if(scriptedResize[0]<=0 || scriptedResize[1]<=0)throw std::invalid_argument("Resize dimensions must be positive");}
        else if(argument=="--time" && i+1<argc)fixedNativeTime=std::stof(argv[++i]);
        else if(argument=="--screenshot" && i+1<argc)nativeScreenshot=argv[++i];
        else if(argument=="--frames"&&i+1<argc){frameLimit=std::stoi(argv[++i]);if(frameLimit<=0)throw std::invalid_argument("Frames must be positive");}
        else throw std::invalid_argument("Unknown argument: "+argument);
    }
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
    if(!classicScene.empty())scene=render::makeClassicScene(classicScene);
    else if(forceDemo || !std::filesystem::exists(config->scene_file)) {
        std::cout << "Loading the built-in RHI feature scene\n";
        scene=rhi::usesNativeRenderer()?render::makeFeatureScene():render::makeForwardDemoScene();
    } else Loader::GetInstance()->loadSceneAsync(scene,config->scene_file);

    RenderManager::GetInstance()->setting.useDefer=!startForward;
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
