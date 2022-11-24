#include"RenderScene.h"
#include"../object/Terrain.h"
#include"../component/GameObject.h"
#include"../object/SkyBox.h"
#include"../component/Lights.h"

using std::shared_ptr;

std::shared_ptr<RenderScene> RenderScene::addObject(std::shared_ptr<GameObject> object) {
	//
	objects.push_back(object);
	if (object->GetComponent("PointLight")) {
		std::shared_ptr<PointLight> light = std::static_pointer_cast<PointLight> (object->GetComponent("PointLight"));
		this->pointLights.push_back(light);
	}
	if (object->GetComponent("DirectionLight")) {
		std::shared_ptr<DirectionLight> light = std::static_pointer_cast<DirectionLight> (object->GetComponent("DirectionLight"));
		this->directionLights.push_back(light);
	}
	//
	return shared_from_this();
}
std::shared_ptr<RenderScene> RenderScene::addTerrain(std::shared_ptr<Terrain>terrain) {
	//
	this->terrain = terrain;
	// 
	return shared_from_this();
}
std::shared_ptr<RenderScene> RenderScene::addSky(std::shared_ptr<Sky>sky) {
	//
	this->sky = sky;
	//
	return shared_from_this();
}

void RenderScene::loadFromJson(json& data) {
	auto& objects = data["objects"];
	for (auto iter = objects.begin(); iter != objects.end(); ++iter) {
		auto object_path = iter.value().get <std::string>();
		std::ifstream f(object_path);
		json data = json::parse(f);
		std::shared_ptr<GameObject> object = std::make_shared<GameObject>();
		object->loadFromJson(data);

		this->addObject(object);
	}
}

void RenderScene::destroy() {
	//
	terrain = nullptr;
	std::vector<shared_ptr<GameObject>>().swap(objects);
	skybox = nullptr;
	std::vector<shared_ptr<DirectionLight>>().swap(directionLights);
	std::vector<shared_ptr<PointLight>>().swap(pointLights);
	//main_camera = nullptr;
}
