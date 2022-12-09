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
#include"./component/Model.h"
//object
#include"object/SkyBox.h"
#include"object/Terrain.h"
//renderer
#include"renderer/Material.h"
#include"renderer/ResourceManager.h"
#include"./renderer/Mesh_Filter.h"
#include"renderer/Mesh_Renderer.h"
#include"./renderer/RenderScene.h"
//system
#include"system/InputManager.h"
#include"./system/meta_register.h"
#include"./system/RenderManager.h"
#include"GUI.h"
#include"./system/global_context.h"
#include"./component/Atmosphere.h"
#include"./system/ModelLoader.h"
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

//#define TEST
//#ifndef TEST
void loadModel(std::shared_ptr<RenderScene> scene);
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
	std::shared_ptr<RenderScene> scene = std::make_shared<RenderScene>();
	// Camera
	{
		std::shared_ptr<Camera> camera = std::make_shared<Camera>();
		scene->main_camera = camera;
	}
	//gui
	Gui gui(window);

	modelLoader->loadSceneAsync(scene, "./asset/objects/scene.json");
			
	while (!glfwWindowShouldClose(window)) {
		gui.window(scene);
		glfwPollEvents();
		//input manager tick
		inputManager->tick();
		
		glCheckError();
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

void loadModel(std::shared_ptr<RenderScene> scene) {
	//// ��ȡԤ�����ļ�
	//auto building = YAML::LoadAllFromFile("./asset/model/PFB_Building_Full.yml");
	//const std::string tag = "tag:unity3d.com,2011:";
	//auto comps = std::unordered_map<std::string, YAML::Node>();
	//auto objAnchors = std::set<std::string>();
	//for (auto comp : building)
	//{
	//	comps.emplace(comp.Anchor(), comp);
	//	if (comp.Tag() == tag + "1")
	//		objAnchors.emplace(comp.Anchor());
	//}

	//// �ڳ����д�������
	//for (auto objAnchor : objAnchors)
	//{
	//	// ���Ҹ�������������
	//	auto& obj = comps[objAnchor];
	//	auto obj_comps = std::unordered_map<std::string, YAML::Node>();
	//	for (auto comp : obj["GameObject"]["m_Component"])
	//	{
	//		std::string anchor = comp["component"]["fileID"].as<std::string>();
	//		std::string tag = comps[anchor].Tag().substr(comps[anchor].Tag().find_last_of(':') + 1);
	//		if (tag == "4")
	//			obj_comps.emplace("Transform", comps[anchor]);
	//		else if (tag == "33")
	//			obj_comps.emplace("MeshFilter", comps[anchor]);
	//		else if (tag == "23")
	//			obj_comps.emplace("MeshRenderer", comps[anchor]);
	//	}
	//	if (obj_comps.find("MeshFilter") == obj_comps.end() ||
	//		obj_comps.find("MeshRenderer") == obj_comps.end())
	//		continue;

	//	// ����λ��
	//	std::shared_ptr<GameObject> model = std::make_shared<GameObject>();
	//	auto&& trans = model->addComponent(Transform::GetWorldTransform(comps, obj_comps["Transform"].Anchor()));

	//	// ��������
	//	std::string guid = obj_comps["MeshFilter"]["MeshFilter"]["m_Mesh"]["guid"].as<std::string>();
	//	auto& meshfilter = model->addComponent<MeshFilter>();
	//	meshfilter->addMesh(Model::loadModel(resourceManager->guidMap[guid], false));
	//	// ������ʣ����ѭ��ȷ��ֻ����һ�Σ�
	//	for (auto mat : obj_comps["MeshRenderer"]["MeshRenderer"]["m_Materials"])
	//		guid = mat["guid"].as<std::string>();
	//	auto&& renderer = model->addComponent<MeshRenderer>();
	//	renderer->setShader(ShaderType::PBR);
	//	renderer->setMaterial(Material::loadModel(resourceManager->guidMap[guid]));
	//	renderer->setDrawMode(GL_TRIANGLES);
	//	//renderer->setPolyMode(GL_LINE);
	//	scene->addObject(model);
	//}
}
