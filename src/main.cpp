#include<glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
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

extern "C" __declspec(dllexport) long long NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;

const unsigned int  SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 900;

// manager
//#define TEST
//#ifndef TEST

shared_ptr<RenderScene> scene;

void RealTimeRun(GLFWwindow* window, shared_ptr<RenderScene>& scene) {
	
	
	//gui
	Gui gui(window);

	// model loading
			
	while (!glfwWindowShouldClose(window)) {
		gui.window(scene);
		glfwPollEvents();
		//input manager tick
		InputManager::GetInstance()->tick();

		if(InputManager::GetInstance()->keyStatus[KEY_R] == PRESSED){
			//阻塞
			Connector::GetInstance()->LaunchPathTracingWithRenderScene(scene);
		}

		// camera tick
		if (scene->main_camera) {
			scene->main_camera->tick();
		}

		RenderManager::GetInstance()->render(scene);

		gui.render();
		InputManager::GetInstance()->reset();
		glfwSwapBuffers(window);
	}
	//glDeleteBuffers()
	gui.destroy();
	glfwDestroyWindow(window);
	glfwTerminate();
}

int main(int argc, char** argv[]) {
	// 
	// render();
	//test();

	auto config = Config::GetInstance();
	config->parse("./config.json");

	glfwInit();
	GLFWwindow* window; 
	createWindow(window, SCR_WIDTH, SCR_HEIGHT);
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	
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
	Loader::GetInstance()->loadSceneAsync(scene, config->scene_file);

	if(config->bGui){
		RealTimeRun(window,scene);
	}
	else {
		Connector::GetInstance()->LaunchPathTracingWithRenderScene(scene);
	}

	return 0; 
}

