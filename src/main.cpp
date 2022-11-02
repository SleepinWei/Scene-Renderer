#include<glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include<iostream>
#include<glfw/glfw3.h>
#include<glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
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
// yaml
#include"../include/yaml-cpp/yaml.h"
extern "C" __declspec(dllexport) long long NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;

const unsigned int  SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 900;

// manager
extern std::unique_ptr<RenderManager> renderManager;
extern std::unique_ptr<ResourceManager> resourceManager;
extern std::unique_ptr<InputManager> inputManager;
//std::unique_ptr<MetaRegister> meta;

//#define TEST
#ifndef TEST

void render() {
	glfwInit();
	GLFWwindow* window; 
	createWindow(window, SCR_WIDTH, SCR_HEIGHT);
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	
	//glad
	gladInit();
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glEnable(GL_PROGRAM_POINT_SIZE);

	// gui 
	Gui gui(window);

	// init Managers 
	{
		inputManager = std::make_unique<InputManager>();
		renderManager = std::make_unique<RenderManager>();
		resourceManager = std::make_unique<ResourceManager>();
	}

	std::shared_ptr<RenderScene> scene = std::make_shared<RenderScene>();
	//skybox
	{
		std::shared_ptr<SkyBox> skybox = std::make_shared<SkyBox>();
		skybox->addShader(ShaderType::SKYBOX) // define shader
			->addMaterial("./asset/skybox/"); // define texture
		scene->addSkyBox(skybox);
	}
		
	//terrain
	if (0)
	{
		std::shared_ptr<Terrain> terrain = std::make_shared<Terrain>();
		terrain->loadHeightmap("./asset/heightmap/mountain/");
		auto terrainMaterial = Material::loadPBR("./asset/heightmap/mountain/");
		terrain
			->addMaterial(terrainMaterial)
			->addShader(ShaderType::TERRAIN);
		//terrain->setPolyMode(GL_LINE);

		scene->addTerrain(terrain);
	}

	// light 
	{
		std::shared_ptr<GameObject> pLight = std::make_shared<GameObject>();
		std::shared_ptr<PointLight>&& light = pLight->addComponent<PointLight>(); 
		light->data.color = glm::vec3(1.0f);

		auto&& transform = pLight->addComponent<Transform>(); 
		transform->scale = glm::vec3(0.1);
		transform->position = glm::vec3(0.0f,0.05f, -2.0f);

		auto&& mesh = pLight->addComponent<MeshFilter>();
		mesh->addShape(SHAPE::POINT);
		
		auto&& renderer = pLight->addComponent <MeshRenderer>();
		renderer->setShader(ShaderType::LIGHT)
			->setDrawMode(GL_POINTS);

		scene->addObject(pLight);
	}
	{
		std::shared_ptr<GameObject> dLight = std::make_shared<GameObject>();

		auto&& transform = dLight->addComponent<Transform>(); 
		transform->scale = glm::vec3(0.1);
		transform->position = glm::vec3(0.0f, 2.0f, 0.0f);

		std::shared_ptr<DirectionLight>&& light = dLight->addComponent<DirectionLight>(); 
		light->data.direction = transform->position;
		float intensity = 1.0f;
		light->data.color = glm::vec3(1.0f, 1.0f, 1.0f) * intensity;

		auto&& mesh = dLight->addComponent<MeshFilter>();
		mesh->addShape(SHAPE::POINT);
		
		auto&& renderer = dLight->addComponent<MeshRenderer>();
		renderer->setShader(ShaderType::LIGHT)
			->setDrawMode(GL_POINTS);

		scene->addObject(dLight);
	}

	// Camera
	{
		std::shared_ptr<Camera> camera = std::make_shared<Camera>();
		scene->main_camera = camera;
	}

	// object
	if (0)
	{
		std::shared_ptr<GameObject> sphere = std::make_shared<GameObject>();
		auto&& transform = sphere->addComponent<Transform>();
		transform->position = glm::vec3(0.0f, 1.0f, 0.0f);
		transform->scale = glm::vec3(0.5);

		auto&& mesh = sphere->addComponent<MeshFilter>();
		mesh->addShape(SHAPE::SPHERE);

		auto&& renderer = sphere->addComponent<MeshRenderer>();
		renderer->setShader(ShaderType::PBR)
			->setMaterial(Material::loadPBR("./asset/pbr/riverrock/"))
			->setDrawMode(GL_TRIANGLES);
			//->setPolyMode(GL_LINE);
		scene->addObject(sphere);
	}
	if (0)
	{
		std::shared_ptr<GameObject> sphere = std::make_shared<GameObject>();
		auto&& transform = sphere->addComponent<Transform>();
		transform->position = glm::vec3(-2.0f, 1.0f, 0.0f);
		transform->scale = glm::vec3(0.5);

		auto&& mesh = sphere->addComponent<MeshFilter>();
		mesh->addShape(SHAPE::SPHERE);

		auto&& renderer = sphere->addComponent<MeshRenderer>();
		renderer->setShader(ShaderType::PBR)
			->setMaterial(Material::loadPBR("./asset/pbr/granite/"))
			->setDrawMode(GL_TRIANGLES);
			//->setPolyMode(GL_LINE);
		scene->addObject(sphere);
	}
	if (0)
	{
		std::shared_ptr<GameObject> sphere = std::make_shared<GameObject>();
		auto&& transform = sphere->addComponent<Transform>();
		transform->position = glm::vec3(2.0f, 1.0f, 0.0f);
		transform->scale = glm::vec3(0.5);

		auto&& mesh = sphere->addComponent<MeshFilter>();
		mesh->addShape(SHAPE::SPHERE);

		auto&& renderer = sphere->addComponent<MeshRenderer>();
		renderer->setShader(ShaderType::PBR_TESS)
			->setMaterial(Material::loadPBR("./asset/pbr/badland/"))
			->setDrawMode(GL_PATCHES);
			//->setPolyMode(GL_LINE);
		scene->addObject(sphere);
	}
	if (0)
	{
		std::shared_ptr<GameObject> sphere = std::make_shared<GameObject>();
		auto&& transform = sphere->addComponent<Transform>();
		transform->position = glm::vec3(4.0f, 1.0f, 0.0f);
		transform->scale = glm::vec3(0.5);

		auto&& mesh = sphere->addComponent<MeshFilter>();
		mesh->addShape(SHAPE::SPHERE);

		auto&& renderer = sphere->addComponent<MeshRenderer>();
		renderer->setShader(ShaderType::PBR)
			->setMaterial(Material::loadPBR("./asset/pbr/desertrock/"))
			->setDrawMode(GL_TRIANGLES);
			//->setPolyMode(GL_LINE);
		scene->addObject(sphere);
	}
#pragma region model
	if (0)
	{
		std::shared_ptr<GameObject> model = std::make_shared<GameObject>();
		auto&& transform = model->addComponent<Transform>();
		transform->position = glm::vec3(0.0f, 0.0f, -2.0f);
		transform->scale = glm::vec3(0.5);

		std::string dir = "./asset/model/backpack/";
		model->addComponent<MeshFilter>(Model::loadModel(dir + "backpack.obj"));
		auto&& mesh = std::dynamic_pointer_cast<MeshFilter>(model->GetComponent("MeshFilter"));

		auto&& renderer = model->addComponent<MeshRenderer>();
		renderer->setShader(ShaderType::PBR)
			->setMaterial(Material::loadCustomModel(dir))
			->setDrawMode(GL_TRIANGLES);
			//->setPolyMode(GL_LINE);
		scene->addObject(model);
	}
	if (0)
	{
		std::shared_ptr<GameObject> model = std::make_shared<GameObject>();
		auto&& transform = model->addComponent<Transform>();
		transform->position = glm::vec3(0.0f, -5.0f, -10.0f);
		transform->scale = glm::vec3(0.02);

		std::string dir = "./asset/model/bed/";
		model->addComponent<MeshFilter>(Model::loadModel(dir + "Bed.fbx", false));
		auto&& mesh = std::dynamic_pointer_cast<MeshFilter>(model->GetComponent("MeshFilter"));

		auto&& renderer = model->addComponent<MeshRenderer>();
		renderer->setShader(ShaderType::PBR)
			->setMaterial(Material::loadCustomModel(dir))
			->setDrawMode(GL_TRIANGLES);
		//->setPolyMode(GL_LINE);
		scene->addObject(model);
	}
	
	// 读取预制体文件
	auto building = YAML::LoadAllFromFile("./asset/model/PFB_Building_Full.yml");
	auto objs = vector<std::unordered_map<std::string, YAML::Node>>();
	int componentCount = 0;
	for (auto obj : building)
	{
		// 读取新物体
		if (componentCount == 0)
		{
			componentCount = obj["GameObject"]["m_Component"].size();
			objs.push_back(std::unordered_map<std::string, YAML::Node>());
		}
		// 为物体添加组件
		else
		{
			for (auto com : obj)
			{
				std::string key = com.first.as<std::string>();
				YAML::Node value = com.second;
				objs[objs.size() - 1].emplace(key, value);
			}
			componentCount--;
		}
	}

	// 在场景中创建物体
	for (auto obj : objs)
	{
		if (obj.find("MeshFilter") == obj.end() || obj.find("MeshRenderer") == obj.end())
			continue;
		// 设置位置
		std::shared_ptr<GameObject> model = std::make_shared<GameObject>();
		auto&& trans = model->addComponent<Transform>();
		YAML::Node pos = obj["Transform"]["m_LocalPosition"];
		YAML::Node rot = obj["Transform"]["m_LocalRotation"];
		YAML::Node scale = obj["Transform"]["m_LocalScale"];
		float scaler = 0.012;
		trans->position = glm::vec3(pos["x"].as<float>(), pos["y"].as<float>(), pos["z"].as<float>());
		trans->rotation = glm::vec3(glm::eulerAngles(glm::quat(rot["w"].as<float>(), rot["x"].as<float>(), rot["y"].as<float>(), rot["z"].as<float>())));
		trans->rotation.x = glm::degrees(trans->rotation.x);
		trans->rotation.y = -glm::degrees(trans->rotation.y);
		trans->rotation.z = glm::degrees(trans->rotation.z);
		trans->scale = glm::vec3(scale["x"].as<float>() * scaler, scale["y"].as<float>() * scaler, scale["z"].as<float>() * scaler);
		// 导入网格
		std::string guid = obj["MeshFilter"]["m_Mesh"]["guid"].as<std::string>();
		model->addComponent<MeshFilter>(Model::loadModel(resourceManager->guidMap[guid], false));
		// 导入材质
		for (auto mat : obj["MeshRenderer"]["m_Materials"])
			guid = mat["guid"].as<std::string>();
		auto&& renderer = model->addComponent<MeshRenderer>();
		renderer->setShader(ShaderType::PBR);
		renderer->setMaterial(Material::loadModel(resourceManager->guidMap[guid]));
		renderer->setDrawMode(GL_TRIANGLES);
		//renderer->setPolyMode(GL_LINE);
		scene->addObject(model);
	}

	//plane
	if (0)
	{
		std::shared_ptr<GameObject> plane = std::make_shared<GameObject>();
		auto&& transform = plane->addComponent<Transform>();
		transform->position = glm::vec3(0.0f, 0.0f, 0.0f);
		transform->scale = glm::vec3(0.2);

		auto&& mesh = plane->addComponent<MeshFilter>();
		mesh->addShape(SHAPE::PLANE);

		auto&& renderer = plane->addComponent<MeshRenderer>();
		renderer->setShader(ShaderType::PBR)
			->setMaterial(Material::loadPBR("./asset/pbr/badland/"))
			->setDrawMode(GL_TRIANGLES);
			//->setPolyMode(GL_LINE);
		// std::cout << resourceManager->resource.size() << '\n';

		scene->addObject(plane);
	}
#pragma endregion

	// atmosphere
	float* sunAngle;
	AtmosphereParameters* parameters; 
	{
		std::shared_ptr<GameObject> atm = std::make_shared<GameObject>();
		auto&& atmosphere = atm->addComponent<Atmosphere>();
		sunAngle = &(atmosphere->sunAngle);
		parameters = &(atmosphere->atmosphere);

		scene->addObject(atm);
	}
	glCheckError();
	
	while (!glfwWindowShouldClose(window)) {
		//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		//glClearColor(0.6, 0.6, 0.6, 1.0);

		gui.window(scene,sunAngle,parameters);
		glfwPollEvents();
		//input manager tick
		inputManager->tick();
		
		// camera tick
		scene->main_camera->tick();

		glCheckError();
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
#endif

void test_print(glm::mat4& model) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			std::cout << model[i][j] << ' '; 
		}
		std::cout << '\n';
	}
}

//#include"PathTracing.h"
int main() {
	// 
	render();
	//test();
	//PT::render();
	return 0; 
}