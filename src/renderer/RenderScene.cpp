#include "renderer/RenderScene.h"
#include "object/Terrain.h"
#include "component/GameObject.h"
#include "object/SkyBox.h"
#include "component/Lights.h"
#include <fstream>
RenderScene::RenderScene()=default;
void RenderScene::checkLogicThread()const{if(logicThread_!=std::this_thread::get_id())throw std::logic_error("Mutable scene belongs to its logic thread; publish a detached snapshot for rendering");}
std::shared_ptr<RenderScene> RenderScene::addObject(std::shared_ptr<GameObject> object){
    checkLogicThread();if(!object)throw std::invalid_argument("Cannot add null object");
    std::scoped_lock lock(mtx,lightMtx);objects.push_back(object);
    if(auto light=object->getComponent<PointLight>())pointLights.push_back(light);
    if(auto light=object->getComponent<DirectionLight>())directionLights.push_back(light);
    if(auto light=object->getComponent<SpotLight>())spotLights.push_back(light);
    ++revision_;return shared_from_this();
}
std::shared_ptr<RenderScene> RenderScene::addTerrain(std::shared_ptr<Terrain> value){checkLogicThread();std::lock_guard<std::mutex> lock(mtx);terrain=std::move(value);++revision_;return shared_from_this();}
std::shared_ptr<RenderScene> RenderScene::addSky(std::shared_ptr<Sky> value){checkLogicThread();std::lock_guard<std::mutex> lock(mtx);sky=std::move(value);++revision_;return shared_from_this();}
void RenderScene::replaceWith(RenderScene& staging){
    checkLogicThread();if(this==&staging)return;
    std::scoped_lock lock(mtx,lightMtx,staging.mtx,staging.lightMtx);
    objects=std::move(staging.objects);directionLights=std::move(staging.directionLights);
    pointLights=std::move(staging.pointLights);spotLights=std::move(staging.spotLights);
    sky=std::move(staging.sky);terrain=std::move(staging.terrain);preparedAssets=std::move(staging.preparedAssets);
    if(staging.main_camera)main_camera=std::move(staging.main_camera);
    ++revision_;
}
void RenderScene::loadFromJson(json& data){
    auto staging=std::make_shared<RenderScene>();
    for(const auto& entry:data.at("objects").items()){
        auto path=entry.value().get<std::string>();std::ifstream file(path);if(!file)throw std::runtime_error("Cannot open object: "+path);
        auto data=json::parse(file);auto object=std::make_shared<GameObject>();object->loadFromJson(data);staging->addObject(object);
    }
    replaceWith(*staging);
}
void RenderScene::destroy(){checkLogicThread();std::scoped_lock lock(mtx,lightMtx);terrain.reset();sky.reset();preparedAssets.reset();objects.clear();directionLights.clear();pointLights.clear();spotLights.clear();++revision_;}
