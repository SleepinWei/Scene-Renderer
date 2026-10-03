#include "renderer/RenderScene.h"
#include "object/Terrain.h"
#include "component/GameObject.h"
#include "object/SkyBox.h"
#include "component/Lights.h"
#include <fstream>
#include <algorithm>
#include "component/Transform.h"
#include "utils/Camera.h"
#include <type_traits>
RenderScene::RenderScene() = default;
void RenderScene::checkLogicThread() const {
    if (transferSealed_ || logicThread_ != std::this_thread::get_id())
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
    object->bindScene(shared_from_this());
    objects_.push_back(object);
    objectIndex_[object->assetId] = object;
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
    auto removed = *found;
    objects_.erase(found);
    if (!ownsObject(removed.get()))
        removed->unbindScene(this);
    rebuildObjectIndex();
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
    for (const auto &object : objects_)
        if (object != terrain_ && object != sky_)
            object->unbindScene(this);
    objects_.clear();
    rebuildObjectIndex();
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
    if (value)
        value->bindScene(shared_from_this());
    auto previous = terrain_;
    terrain_ = std::move(value);
    if (previous && !ownsObject(previous.get()))
        previous->unbindScene(this);
    rebuildObjectIndex();
    ++revision_;
    return shared_from_this();
}
std::shared_ptr<RenderScene> RenderScene::addSky(std::shared_ptr<Sky> value) {
    checkLogicThread();
    std::lock_guard<std::mutex> lock(structureMutex_);
    if (value)
        value->bindScene(shared_from_this());
    auto previous = sky_;
    sky_ = std::move(value);
    if (previous && !ownsObject(previous.get()))
        previous->unbindScene(this);
    rebuildObjectIndex();
    ++revision_;
    return shared_from_this();
}
void RenderScene::setCamera(std::shared_ptr<Camera> value) {
    checkLogicThread();
    if (value)
        value->checkLogicThread();
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
    if (staging.logicThread_ != std::this_thread::get_id() && !staging.transferSealed_)
        throw std::logic_error("Foreign staging must be sealed before publication");
    commands_->invalidate();
    detachObjects();
    objects_ = std::move(staging.objects_);
    directionLights_ = std::move(staging.directionLights_);
    pointLights_ = std::move(staging.pointLights_);
    spotLights_ = std::move(staging.spotLights_);
    sky_ = std::move(staging.sky_);
    terrain_ = std::move(staging.terrain_);
    preparedAssets_ = std::move(staging.preparedAssets_);
    if (staging.camera_) {
        staging.camera_->adoptOwnership();
        camera_ = std::move(staging.camera_);
    }
    auto self = shared_from_this();
    for (const auto &object : objects_)
        object->bindScene(self, &staging);
    if (terrain_)
        terrain_->bindScene(self, &staging);
    if (sky_)
        sky_->bindScene(self, &staging);
    rebuildObjectIndex();
    staging.objectIndex_.clear();
    ++revision_;
}
void RenderScene::sealForTransfer() {
    checkLogicThread();
    std::lock_guard<std::mutex> lock(structureMutex_);
    for (const auto &entry : objectIndex_)
        entry.second->sealForTransfer(this);
    if (camera_)
        camera_->sealOwnership();
    commands_->close();
    transferSealed_ = true;
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
    commands_->invalidate();
    detachObjects();
    terrain_.reset();
    sky_.reset();
    preparedAssets_.reset();
    objects_.clear();
    rebuildLightIndex();
    ++revision_;
}

bool RenderScene::ownsObject(const GameObject *object) const {
    return terrain_.get() == object || sky_.get() == object ||
           std::any_of(objects_.begin(), objects_.end(),
                       [object](const auto &value) { return value.get() == object; });
}
void RenderScene::rebuildObjectIndex() {
    objectIndex_.clear();
    for (const auto &object : objects_)
        objectIndex_[object->assetId] = object;
    if (terrain_)
        objectIndex_[terrain_->assetId] = terrain_;
    if (sky_)
        objectIndex_[sky_->assetId] = sky_;
}
void RenderScene::detachObjects() {
    for (const auto &entry : objectIndex_)
        entry.second->unbindScene(this);
    objectIndex_.clear();
}
void RenderScene::objectComponentsChanged(uint64_t id) {
    checkLogicThread();
    std::lock_guard<std::mutex> lock(structureMutex_);
    if (!objectIndex_.count(id))
        throw std::logic_error("Component update refers to an unpublished object");
    rebuildLightIndex();
    preparedAssets_.reset();
    ++revision_;
}
std::shared_ptr<GameObject> RenderScene::findObject(uint64_t id) const {
    checkLogicThread();
    auto found = objectIndex_.find(id);
    return found == objectIndex_.end() ? nullptr : found->second;
}
engine::CommandPort<engine::WorldCommand> RenderScene::commandPort() const {
    checkLogicThread();
    return commands_->port();
}
size_t RenderScene::applyCommands(size_t limit) {
    checkLogicThread();
    return commands_->drain(limit, [this](const engine::WorldCommand &command) -> engine::CommandResult {
        return std::visit(
            [this](const auto &value) -> engine::CommandResult {
                using T = std::decay_t<decltype(value)>;
                using S = engine::CommandStatus;
                std::shared_ptr<GameObject> object = findObject(value.objectId);
                if (!object)
                    return {S::MissingTarget, "Object no longer belongs to this world"};
                if constexpr (std::is_same_v<T, engine::SetTransform>) {
                    auto transform = object->getComponent<Transform>();
                    if (!transform || transform->assetId != value.componentId)
                        return {S::MissingTarget, "Transform was removed or replaced"};
                    try {
                        transform->setTRS(value.position, value.rotation, value.scale);
                    } catch (const std::invalid_argument &error) {
                        return {S::Invalid, error.what()};
                    }
                } else if constexpr (std::is_same_v<T, engine::RemoveObject>) {
                    if (!removeObject(value.objectId))
                        return {S::MissingTarget, "Object is not in the removable object list"};
                } else if constexpr (std::is_same_v<T, engine::RemoveComponent>) {
                    if (!object->removeComponent(value.componentId))
                        return {S::MissingTarget, "Component was removed or replaced"};
                } else if constexpr (std::is_same_v<T, engine::SetDeferred>)
                    object->setDeferred(value.deferred);
                return {S::Applied, {}};
            },
            command);
    });
}
