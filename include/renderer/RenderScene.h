#pragma once
#include "engine/AssetIdentity.h"
#include <atomic>
#include <thread>
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
class SpotLight;
class Sky;
namespace render {struct RenderWorldSnapshot;}

class RenderScene : public engine::AssetIdentity, public std::enable_shared_from_this<RenderScene> {
	// scene objects
public:
	std::shared_ptr<Terrain> terrain;
	std::vector<std::shared_ptr<GameObject>> objects;
	std::shared_ptr<Sky> sky;
	std::vector<std::shared_ptr<DirectionLight>> directionLights;
	std::vector<std::shared_ptr<PointLight>> pointLights;
	std::vector<std::shared_ptr<SpotLight>> spotLights;

	std::shared_ptr<Camera> main_camera;
    // Validated immutable CPU assets from the loader. No GPU objects here.
    std::shared_ptr<const render::RenderWorldSnapshot> preparedAssets;

public:
	RenderScene();
	std::shared_ptr<RenderScene> addObject(std::shared_ptr<GameObject> object);
	std::shared_ptr<RenderScene> addTerrain(std::shared_ptr<Terrain>terrain);
	std::shared_ptr<RenderScene> addSky(std::shared_ptr<Sky>skybox);
	void loadFromJson(json& data);
	void destroy();
    void replaceWith(RenderScene& staging); // Logic thread: atomic structural publication.
    void checkLogicThread() const;
    uint64_t revision()const{return revision_.load();}
private:
    std::atomic<uint64_t> revision_{0};
    const std::thread::id logicThread_=std::this_thread::get_id();
public:
	std::mutex mtx;
	std::mutex lightMtx;
};

//namespace RenderPass {
//
//	void shadowPass(std::shared_ptr<RenderScene> scene);
//	void forwardPass(std::shared_ptr<RenderScene> scene);
//	void deferredPass(std::shared_ptr<RenderScene> scene);
//};
