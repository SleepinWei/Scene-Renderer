#include<glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include<iostream>
#include<fstream>
#include<glfw/glfw3.h>
#include<glm/glm.hpp>
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
//extern std::shared_ptr<RenderScene> renderScene;
//std::unique_ptr<MetaRegister> meta;
std::shared_ptr<GameObject> loadFromJson(const std::string& path);

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
	//glDepthMask(GL_FALSE);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glEnable(GL_PROGRAM_POINT_SIZE);
	// gui
	Gui gui(window);
	
	// init Managers 
	{
		inputManager = std::make_unique<InputManager>();
		renderManager = std::make_unique<RenderManager>();
		renderManager->init();
		resourceManager = std::make_unique<ResourceManager>();
		//renderScene = std::make_shared<RenderScene>();
	}
	//std::shared_ptr<RenderScene>& scene = renderScene;
	std::shared_ptr<RenderScene> scene = std::make_shared<RenderScene>();

	//skybox
	if(0)
	{
		//std::shared_ptr<SkyBox> skybox = std::make_shared<SkyBox>();
		//skybox->addShader(ShaderType::SKYBOX) // define shader
			//->addMaterial("./asset/skybox/"); // define texture
		//scene->addSkyBox(skybox);
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
	if(0)
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

	//model
#pragma region model
	if (0)
	{
		std::shared_ptr<GameObject> model = std::make_shared<GameObject>();
		auto&& transform = model->addComponent<Transform>();
		transform->position = glm::vec3(0.0f, 0.0f, -2.0f);
		transform->scale = glm::vec3(0.5);

		std::string dir = "./asset/model/backpack/";
		auto& meshfilter = model->addComponent<MeshFilter>();
		meshfilter->addMesh(Model::loadModel(dir + "backpack.obj"));
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
		auto& meshfilter = model->addComponent<MeshFilter>();
		meshfilter->addMesh(Model::loadModel(dir + "Bed.fbx", false));
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
		// ��ȡԤ�����ļ�
		auto building = YAML::LoadAllFromFile("./asset/model/PFB_Building_Full.yml");
		const std::string tag = "tag:unity3d.com,2011:";
		auto comps = std::unordered_map<std::string, YAML::Node>();
		auto objAnchors = std::set<std::string>();
		for (auto comp : building)
		{
			comps.emplace(comp.Anchor(), comp);
			if (comp.Tag() == tag + "1")
				objAnchors.emplace(comp.Anchor());
		}

		// �ڳ����д�������
		for (auto objAnchor : objAnchors)
		{
			// ���Ҹ�������������
			auto& obj = comps[objAnchor];
			auto obj_comps = std::unordered_map<std::string, YAML::Node>();
			for (auto comp : obj["GameObject"]["m_Component"])
			{
				std::string anchor = comp["component"]["fileID"].as<std::string>();
				std::string tag = comps[anchor].Tag().substr(comps[anchor].Tag().find_last_of(':') + 1);
				if (tag == "4")
					obj_comps.emplace("Transform", comps[anchor]);
				else if (tag == "33")
					obj_comps.emplace("MeshFilter", comps[anchor]);
				else if (tag == "23")
					obj_comps.emplace("MeshRenderer", comps[anchor]);
			}
			if (obj_comps.find("MeshFilter") == obj_comps.end() ||
				obj_comps.find("MeshRenderer") == obj_comps.end())
				continue;

			// ����λ��
			std::shared_ptr<GameObject> model = std::make_shared<GameObject>();
			auto&& trans = model->addComponent(Transform::GetWorldTransform(comps, obj_comps["Transform"].Anchor()));

			// ��������
			std::string guid = obj_comps["MeshFilter"]["MeshFilter"]["m_Mesh"]["guid"].as<std::string>();
			auto& meshfilter = model->addComponent<MeshFilter>();
			meshfilter->addMesh(Model::loadModel(resourceManager->guidMap[guid], false));
			// ������ʣ����ѭ��ȷ��ֻ����һ�Σ�
			for (auto mat : obj_comps["MeshRenderer"]["MeshRenderer"]["m_Materials"])
				guid = mat["guid"].as<std::string>();
			auto&& renderer = model->addComponent<MeshRenderer>();
			renderer->setShader(ShaderType::PBR);
			renderer->setMaterial(Material::loadModel(resourceManager->guidMap[guid]));
			renderer->setDrawMode(GL_TRIANGLES);
			//renderer->setPolyMode(GL_LINE);
			scene->addObject(model);
		}
	}

	if (0)
	{
		scene->addObject(loadFromJson("./asset/objects/sphere.json"));
		scene->addObject(loadFromJson("./asset/objects/sphere2.json"));
		scene->addObject(loadFromJson("./asset/objects/backpack.json"));
	}
#pragma endregion
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

	// atmosphere
	float* sunAngle;
	AtmosphereParameters* parameters; 
	{
		//std::shared_ptr<GameObject> atm = std::make_shared<GameObject>();
		//auto&& atmosphere = atm->addComponent<Atmosphere>();

		std::shared_ptr<Sky> sky = std::make_shared<Sky>();

		sunAngle = &(sky->atmosphere->sunAngle);
		parameters = &(sky->atmosphere->atmosphere);

		//scene->addObject(atm);
		scene->addSky(sky);
	}
	glCheckError();
	
	while (!glfwWindowShouldClose(window)) {
		//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		//glClearColor(0.6, 0.6, 0.6, 1.0);
		//glEnable(GL_DEPTH_TEST);

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

#include"PT/PathTracing.h"
int main() {
	// 
	render();
	//test();
	//PT::render();
	return 0; 
}

std::shared_ptr<GameObject> loadFromJson(const std::string& path)
{
	std::ifstream f(path);
	json data = json::parse(f);
	std::shared_ptr<GameObject> object = std::make_shared<GameObject>();
	object->loadFromJson(data);
	return object;
}