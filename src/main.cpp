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
#include"renderer/ResourceManager.h"
#include"renderer/Mesh_Filter.h"
#include"renderer/Mesh_Renderer.h"
#include"renderer/RenderScene.h"
//system
#include"system/InputManager.h"
#include"system/meta_register.h"
#include"system/RenderManager.h"
#include"GUI.h"
#include"system/global_context.h"
#include"component/Atmosphere.h"
#include"system/ModelLoader.h"
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
extern std::unique_ptr<RenderManager> renderManager;
extern std::unique_ptr<ResourceManager> resourceManager;
extern std::unique_ptr<InputManager> inputManager;
extern std::shared_ptr<ModelLoader> modelLoader;
std::shared_ptr<RenderScene> scene;
//#define TEST
//#ifndef TEST
void render() {
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
	// init Managers 
	{
		inputManager = std::make_unique<InputManager>();
		renderManager = std::make_unique<RenderManager>();
		renderManager->init();
		resourceManager = std::make_unique<ResourceManager>();
		modelLoader = std::make_shared<ModelLoader>();
		//renderScene = std::make_shared<RenderScene>();
	}
	scene = std::make_shared<RenderScene>();
	// Camera
	{
		std::shared_ptr<Camera> camera = std::make_shared<Camera>();
		scene->main_camera = camera;
	}
	//gui
	Gui gui(window);

	modelLoader->loadSceneAsync(scene, "./asset/objects/scene2.json");
			
	while (!glfwWindowShouldClose(window)) {
		gui.window(scene);
		glfwPollEvents();
		//input manager tick
		inputManager->tick();
		
		
		// camera tick
		if (scene->main_camera) {
			scene->main_camera->tick();
		}

		renderManager->render(scene);

		gui.render();
		inputManager->reset();
		glfwSwapBuffers(window);
	}
	//glDeleteBuffers()
	gui.destroy();
	glfwDestroyWindow(window);
	glfwTerminate();
}
//#endif

//#include"PT/PathTracing.h"
int main() {
	// 
	render();
	//test();
	//PT::render();
	//std::cout <<glGetString(GL_EXTENSIONS)<< '\n';
	return 0; 
}

