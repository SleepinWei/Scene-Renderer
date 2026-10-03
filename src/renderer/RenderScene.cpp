#include "renderer/RenderScene.h"
#include "object/Terrain.h"
#include "component/GameObject.h"
#include "object/SkyBox.h"
#include "component/Lights.h"
#include <fstream>
#include <algorithm>
RenderScene::RenderScene() = default;
void RenderScene::checkLogicThread() const {
    if (logicThread_ != std::this_thread::get_id())
        throw std::logic_error(
            "Mutable scene belongs to its logic thread; publish a detached snapshot for rendering");
}
void RenderScene::rebuildLightIndex() {
    directionLights_.clear();
    pointLights_.clear();
    spotLights_.clear();
    for (const auto &object : objects_) {
        if (auto light = object->getComponent<DirectionLight>())
            directionLights_.push_back(light);
        if (auto light = object->getComponent<PointLight>())
            pointLights_.push_back(light);
        if (auto light = object->getComponent<SpotLight>())
            spotLights_.push_back(light);
    }
}
std::shared_ptr<RenderScene> RenderScene::addObject(std::shared_ptr<GameObject> object) {
    checkLogicThread();
    if (!object)
        throw std::invalid_argument("Cannot add null object");
    std::lock_guard<std::mutex> lock(structureMutex_);
    if (std::any_of(objects_.begin(), objects_.end(),
                    [&](const auto &value) { return value->assetId == object->assetId; }))
        return shared_from_this();
    objects_.push_back(object);
    if (auto light = object->getComponent<PointLight>())
        pointLights_.push_back(light);
    if (auto light = object->getComponent<DirectionLight>())
        directionLights_.push_back(light);
    if (auto light = object->getComponent<SpotLight>())
        spotLights_.push_back(light);
    ++revision_;
    return shared_from_this();
}
bool RenderScene::removeObject(uint64_t id) {
    checkLogicThread();
    std::lock_guard<std::mutex> lock(structureMutex_);
    auto found = std::find_if(objects_.begin(), objects_.end(),
                              [id](const auto &object) { return object->assetId == id; });
    if (found == objects_.end())
        return false;
    objects_.erase(found);
    rebuildLightIndex();
    preparedAssets_.reset();
    ++revision_;
    return true;
}
void RenderScene::clearObjects() {
    checkLogicThread();
    std::lock_guard<std::mutex> lock(structureMutex_);
    if (objects_.empty())
        return;
    objects_.clear();
    rebuildLightIndex();
    preparedAssets_.reset();
    ++revision_;
}
bool RenderScene::refreshObject(uint64_t id) {
    checkLogicThread();
    std::lock_guard<std::mutex> lock(structureMutex_);
    if (std::none_of(objects_.begin(), objects_.end(),
                     [id](const auto &object) { return object->assetId == id; }))
        return false;
    rebuildLightIndex();
    preparedAssets_.reset();
    ++revision_;
    return true;
}
std::shared_ptr<RenderScene> RenderScene::addTerrain(std::shared_ptr<Terrain> value) {
    checkLogicThread();
    std::lock_guard<std::mutex> lock(structureMutex_);
    terrain_ = std::move(value);
    ++revision_;
    return shared_from_this();
}
std::shared_ptr<RenderScene> RenderScene::addSky(std::shared_ptr<Sky> value) {
    checkLogicThread();
    std::lock_guard<std::mutex> lock(structureMutex_);
    sky_ = std::move(value);
    ++revision_;
    return shared_from_this();
}
void RenderScene::setCamera(std::shared_ptr<Camera> value) {
    checkLogicThread();
    camera_ = std::move(value);
    ++revision_;
}
void RenderScene::setPreparedAssets(std::shared_ptr<const render::RenderWorldSnapshot> value) {
    checkLogicThread();
    preparedAssets_ = std::move(value);
}
void RenderScene::replaceWith(RenderScene &staging) {
    checkLogicThread();
    if (this == &staging)
        return;
    std::scoped_lock lock(structureMutex_, staging.structureMutex_);
    objects_ = std::move(staging.objects_);
    directionLights_ = std::move(staging.directionLights_);
    pointLights_ = std::move(staging.pointLights_);
    spotLights_ = std::move(staging.spotLights_);
    sky_ = std::move(staging.sky_);
    terrain_ = std::move(staging.terrain_);
    preparedAssets_ = std::move(staging.preparedAssets_);
    if (staging.camera_)
        camera_ = std::move(staging.camera_);
    ++revision_;
}
void RenderScene::loadFromJson(json &data) {
    checkLogicThread();
    auto staging = std::make_shared<RenderScene>();
    for (const auto &entry : data.at("objects").items()) {
        auto path = entry.value().get<std::string>();
        std::ifstream file(path);
        if (!file)
            throw std::runtime_error("Cannot open object: " + path);
        auto child = json::parse(file);
        auto object = std::make_shared<GameObject>();
        object->loadFromJson(child);
        staging->addObject(object);
    }
    replaceWith(*staging);
}
void RenderScene::destroy() {
    checkLogicThread();
    std::lock_guard<std::mutex> lock(structureMutex_);
    terrain_.reset();
    sky_.reset();
    preparedAssets_.reset();
    objects_.clear();
    rebuildLightIndex();
    ++revision_;
}
