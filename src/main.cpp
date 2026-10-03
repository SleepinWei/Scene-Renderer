#include<glad/glad.h>
#include "rhi/Device.h"
#include "rhi/GraphicsDevice.h"
#include "rhi/Validation.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/GpuOcean.h"
#include "renderer/rhi/GpuTerrain.h"
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
int maxFramesInFlight=3;
bool hiddenEditor=false;
std::array<int,2> windowSize{1600,900};

void RealTimeRun(GLFWwindow* window, shared_ptr<RenderScene>& scene) {
	
	
	//gui
	Gui gui(window);

	// model loading
			
	int framesRendered=0;const auto started=std::chrono::steady_clock::now();
	while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
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
		if (scene->main_camera) {
			scene->main_camera->tick();
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
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--backend" && i+1<argc){const std::string name=argv[++i];if(name=="Vulkan")rhi::requestBackend(rhi::Backend::Vulkan);else if(name=="Metal")rhi::requestBackend(rhi::Backend::Metal);else if(name=="OpenGL")rhi::requestBackend(rhi::Backend::OpenGL);else throw std::invalid_argument("Unknown backend: "+name);}
#ifndef SCENERENDERER_HAS_VULKAN
    if(rhi::requestedBackend()==rhi::Backend::Vulkan)throw std::invalid_argument("This build excludes Vulkan; enable SCENERENDERER_VULKAN_PROTOTYPE or select the Vulkan backend in CMake");
#endif
#ifdef SCENERENDERER_METAL
    if(rhi::requestedBackend()==rhi::Backend::OpenGL)throw std::invalid_argument("Use an OpenGL CMake build for the legacy OpenGL renderer");
#else
    if(rhi::requestedBackend()==rhi::Backend::Metal)throw std::invalid_argument("Use a Metal CMake build on macOS");
#endif
    if (argc > 1 && (std::string(argv[1]) == "--rhi-forward" || std::string(argv[1]) == "--rhi-deferred" || std::string(argv[1]) == "--rhi-scene")) { render::runForwardScene(argc,argv);return 0; }
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
            render::validateSceneEffects(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        render::validateAtmosphereRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        render::validateTerrainRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        render::validateSubdivisionRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        render::validateTemporalRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
        rhi::validateComputeAndIndirect(*rhi::graphicsDevice());
            render::validateOceanRhi(rhi::graphicsDevice(),rhi::defaultShaderDirectory());
            rhi::validateFrameLifecycle(rhi::graphicsDevice());
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
        else if(argument=="--frames-in-flight" && i+1<argc)maxFramesInFlight=std::stoi(argv[++i]);
        else if(argument=="--size" && i+1<argc){const std::string size=argv[++i];auto x=size.find('x');if(x==std::string::npos)throw std::invalid_argument("Size expects WIDTHxHEIGHT");windowSize={std::stoi(size.substr(0,x)),std::stoi(size.substr(x+1))};if(windowSize[0]<=0 || windowSize[1]<=0)throw std::invalid_argument("Window dimensions must be positive");}
        else if(argument=="--forward")startForward=true;
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
		scene->main_camera = camera;
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
