#include<glad/glad.h>
#ifdef SCENERENDERER_METAL
#include "metal/MetalBackend.h"
#include "metal/MetalDemo.h"
#endif
#include<iostream>
#include<fstream>
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

void RealTimeRun(GLFWwindow* window, shared_ptr<RenderScene>& scene) {
	
	
	//gui
	Gui gui(window);

	// model loading
			
	while (!glfwWindowShouldClose(window)) {

#ifdef SCENERENDERER_METAL
        MetalBackend::beginFrame();
#endif
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
		InputManager::GetInstance()->reset();
#ifdef SCENERENDERER_METAL
        MetalBackend::present();
        if(frameLimit>0 && --frameLimit==0)glfwSetWindowShouldClose(window,true);
#else
		glfwSwapBuffers(window);
#endif
	}
	//glDeleteBuffers()
	gui.destroy();
#ifdef SCENERENDERER_METAL
    MetalBackend::shutdown();
#endif
	glfwDestroyWindow(window);
	glfwTerminate();
}

int main(int argc, char** argv) {
    try {
#ifdef SCENERENDERER_METAL
    if (argc>1 && std::string(argv[1])=="--metal-self-test") { MetalBackend::selfTest(); return 0; }
    if (argc>1 && std::string(argv[1])=="--render-gallery") { renderMetalGallery(argc>2?argv[2]:"img/metal", argc>3?argv[3]:""); return 0; }
    bool forceDemo=false;
    std::string classicScene;
    for(int i=1;i<argc;i++) {
        std::string argument=argv[i];
        if(argument=="--demo")forceDemo=true;
        else if(argument=="--classic"&&i+1<argc)classicScene=argv[++i];
        else if(argument=="--frames"&&i+1<argc)frameLimit=std::stoi(argv[++i]);
    }
#endif
	(void)argc;
	(void)argv;
	// 
	// render();
	//test();

	auto config = Config::GetInstance();
	config->parse("./config.json");

	glfwInit();
	GLFWwindow* window; 
	if(createWindow(window, SCR_WIDTH, SCR_HEIGHT)!=0) return 1;
#ifndef SCENERENDERER_METAL
	glfwMakeContextCurrent(window);
#endif
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
#ifdef SCENERENDERER_METAL
    int framebufferWidth,framebufferHeight;
    glfwGetFramebufferSize(window,&framebufferWidth,&framebufferHeight);
    framebuffer_size_callback(window,framebufferWidth,framebufferHeight);
#endif
	
	//glad
	gladInit();
	glEnable(GL_DEPTH_TEST);
	//glDepthMask(GL_FALSE);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glEnable(GL_PROGRAM_POINT_SIZE);

	// scene loading
	scene = std::make_shared<RenderScene>();
	RenderManager::GetInstance()->init();
	// Camera
	{
		std::shared_ptr<Camera> camera = std::make_shared<Camera>();
		scene->main_camera = camera;
	}
#ifdef SCENERENDERER_METAL
    if(!classicScene.empty()) {
        scene=makeMetalClassicScene(classicScene);
    } else if(forceDemo || !std::filesystem::exists(config->scene_file)) {
        std::cout << "Loading the built-in Metal feature scene\n";
        scene=makeMetalDemoScene();
    } else
#endif
    Loader::GetInstance()->loadSceneAsync(scene, config->scene_file);

	if(config->bGui){
		RealTimeRun(window,scene);
	}
	else {
		Connector::GetInstance()->LaunchPathTracingWithRenderScene(scene);
	}

	return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << "\n";return 1;}
}

