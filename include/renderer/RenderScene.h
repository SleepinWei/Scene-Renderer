#pragma once
#include "engine/AssetIdentity.h"
#include "engine/WorldCommand.h"
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <vector>
#include <json/json.hpp>
#include <mutex>
#include <unordered_map>
using json = nlohmann::json;
class GameObject;
class Terrain;
class Sky;
class Camera;
class PointLight;
class DirectionLight;
class SpotLight;
namespace render {
struct RenderWorldSnapshot;
}

// Structure is private. Access is restricted to the owning logic thread;
// referenced legacy components remain mutable on that thread only.
class RenderScene : public engine::AssetIdentity, public std::enable_shared_from_this<RenderScene> {
  public:
    RenderScene();
    const std::vector<std::shared_ptr<GameObject>> &objects() const {
        checkLogicThread();
        return objects_;
    }
    const std::vector<std::shared_ptr<DirectionLight>> &directionLights() const {
        checkLogicThread();
        return directionLights_;
    }
    const std::vector<std::shared_ptr<PointLight>> &pointLights() const {
        checkLogicThread();
        return pointLights_;
    }
    const std::vector<std::shared_ptr<SpotLight>> &spotLights() const {
        checkLogicThread();
        return spotLights_;
    }
    const std::shared_ptr<Terrain> &terrain() const {
        checkLogicThread();
        return terrain_;
    }
    const std::shared_ptr<Sky> &sky() const {
        checkLogicThread();
        return sky_;
    }
    const std::shared_ptr<Camera> &mainCamera() const {
        checkLogicThread();
        return camera_;
    }
    const std::shared_ptr<const render::RenderWorldSnapshot> &preparedAssets() const {
        checkLogicThread();
        return preparedAssets_;
    }
    std::shared_ptr<RenderScene> addObject(std::shared_ptr<GameObject>);
    std::shared_ptr<RenderScene> addTerrain(std::shared_ptr<Terrain>);
    std::shared_ptr<RenderScene> addSky(std::shared_ptr<Sky>);
    bool removeObject(uint64_t id);
    bool refreshObject(uint64_t id); // Compatibility refresh; component APIs notify automatically.
    std::shared_ptr<GameObject> findObject(uint64_t id) const;
    engine::CommandPort<engine::WorldCommand> commandPort() const;
    size_t applyCommands(size_t limit = 64);
    void clearObjects();
    void setCamera(std::shared_ptr<Camera>);
    void setPreparedAssets(std::shared_ptr<const render::RenderWorldSnapshot>);
    void loadFromJson(json &);
    void destroy();
    // Staging must be detached and its producer joined before this ownership handoff.
    void replaceWith(RenderScene &staging);
    void sealForTransfer(); // Loader: freeze a completed staging before delivering it.
    void checkLogicThread() const;
    uint64_t revision() const { return revision_.load(); }

  private:
    friend class GameObject;
    void objectComponentsChanged(uint64_t id);
    bool ownsObject(const GameObject *) const;
    void rebuildObjectIndex();
    void detachObjects();
    void rebuildLightIndex();
    std::shared_ptr<Terrain> terrain_;
    std::shared_ptr<Sky> sky_;
    std::shared_ptr<Camera> camera_;
    std::vector<std::shared_ptr<GameObject>> objects_;
    std::vector<std::shared_ptr<DirectionLight>> directionLights_;
    std::vector<std::shared_ptr<PointLight>> pointLights_;
    std::vector<std::shared_ptr<SpotLight>> spotLights_;
    std::shared_ptr<const render::RenderWorldSnapshot> preparedAssets_;
    std::atomic<uint64_t> revision_{0};
    const std::thread::id logicThread_ = std::this_thread::get_id();
    std::mutex structureMutex_;
    std::unordered_map<uint64_t, std::shared_ptr<GameObject>> objectIndex_;
    std::shared_ptr<engine::WorldCommandInbox> commands_ = std::make_shared<engine::WorldCommandInbox>(256);
    bool transferSealed_ = false;
};
