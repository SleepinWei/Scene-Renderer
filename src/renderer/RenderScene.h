#pragma once
#include<memory>
#include<string>
#include<vector>
#include<json/json.hpp>
#include<mutex>
using json = nlohmann::json;

class GameObject;
class Terrain; 
class SkyBox;
class Light;
class Camera;
class PointLight;
class DirectionLight;
class Sky;

class RenderScene : public std::enable_shared_from_this<RenderScene> {
	// scene objects
public:
	std::shared_ptr<Terrain> terrain; 
	std::vector<std::shared_ptr<GameObject>> objects;
	std::shared_ptr<Sky> sky;
	std::vector<std::shared_ptr<DirectionLight>> directionLights;
	std::vector<std::shared_ptr<PointLight>> pointLights;

	std::shared_ptr<Camera> main_camera;

public:
	RenderScene();
	std::shared_ptr<RenderScene> addObject(std::shared_ptr<GameObject> object);
	std::shared_ptr<RenderScene> addTerrain(std::shared_ptr<Terrain>terrain);
	std::shared_ptr<RenderScene> addSky(std::shared_ptr<Sky>skybox);
	void loadFromJson(json& data);
	void destroy();
public:
	std::mutex mtx;
};

//namespace RenderPass {
//
//	void shadowPass(std::shared_ptr<RenderScene> scene);
//	void forwardPass(std::shared_ptr<RenderScene> scene);
//	void deferredPass(std::shared_ptr<RenderScene> scene);
//};
