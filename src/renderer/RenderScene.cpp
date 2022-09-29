#include"RenderScene.h"
#include"../object/Terrain.h"
#include"../component/GameObject.h"
#include"../object/SkyBox.h"
#include"../component/Lights.h"

std::shared_ptr<RenderScene> RenderScene::addObject(std::shared_ptr<GameObject> object) {
	objects.push_back(object);
	if (object->GetComponent("PointLight")) {
		std::shared_ptr<PointLight> light = std::dynamic_pointer_cast<PointLight> (object->GetComponent("PointLight"));
		this->pointLights.push_back(light);
	}
	if (object->GetComponent("DirectionLight")) {
		std::shared_ptr<DirectionLight> light = std::dynamic_pointer_cast<DirectionLight> (object->GetComponent("DirectionLight"));
		this->directionLights.push_back(light);
	}
	return shared_from_this();
}
std::shared_ptr<RenderScene> RenderScene::addTerrain(std::shared_ptr<Terrain>terrain) {
	this->terrain = terrain;
	return shared_from_this();
}
std::shared_ptr<RenderScene> RenderScene::addSkyBox(std::shared_ptr<SkyBox>skybox) {
	this->skybox = skybox;
	return shared_from_this();
}

