#include"RenderScene.h"
#include"../object/Terrain.h"
#include"../component/GameObject.h"
#include"../object/SkyBox.h"
#include"../component/Lights.h"

using std::shared_ptr;

RenderScene::RenderScene() {
	sky = nullptr;
	terrain = nullptr;
	main_camera = nullptr;
}

std::shared_ptr<RenderScene> RenderScene::addObject(std::shared_ptr<GameObject> object) {
	//
	mtx.lock();
	totalObjects.emplace_back(object);
	mtx.unlock();

	auto& Plight= object->GetComponent("PointLight");
	if (Plight) {
		std::shared_ptr<PointLight> light = std::static_pointer_cast<PointLight> (Plight);
		lightMtx.lock();
		this->pointLights.emplace_back(light);
		lightMtx.unlock();
	}
	auto& Dlight = object->GetComponent("DirectionLight");
	if (Dlight) {
		std::shared_ptr<DirectionLight> light = std::static_pointer_cast<DirectionLight> (Dlight);
		lightMtx.lock();
		this->directionLights.emplace_back(light);
		lightMtx.unlock();
	}
	auto& Slight = object->GetComponent("SpotLight");
	if (Slight) {
		std::shared_ptr<SpotLight> light = std::static_pointer_cast<SpotLight> (Slight);
		lightMtx.lock();
		this->spotLights.emplace_back(light);
		lightMtx.unlock();
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
	auto& JsonObjects = data["objects"];
	for (auto iter = JsonObjects.begin(); iter != JsonObjects.end(); ++iter) {
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
	std::vector<shared_ptr<GameObject>>().swap(totalObjects);
	std::vector<shared_ptr<GameObject>>().swap(objects);
	std::vector<shared_ptr<DirectionLight>>().swap(directionLights);
	std::vector<shared_ptr<PointLight>>().swap(pointLights);
	sky = nullptr;
	//main_camera = nullptr;
}
