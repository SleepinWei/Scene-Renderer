#pragma once
#include "engine/AssetIdentity.h"
#include "component/Component.h"
#include <map>
#include <typeindex>
#include <unordered_map>
#include <thread>
#include <memory>
#include <string>
#include <json/json.hpp>
class RenderScene;
using json = nlohmann::json;
class GameObject : public engine::AssetIdentity, public std::enable_shared_from_this<GameObject> {
  public:
    GameObject();
    explicit GameObject(std::string);
    GameObject(const GameObject &) = delete;
    GameObject &operator=(const GameObject &) = delete;
    virtual ~GameObject();
    template <class T> std::shared_ptr<T> addComponent() {
        checkLogicThread();
        if (auto existing = getComponent<T>())
            return existing;
        auto component = std::make_shared<T>();
        addComponent(component);
        return component;
    }
    std::shared_ptr<GameObject> addComponent(const std::shared_ptr<Component> &);
    bool removeComponent(uint64_t id);
    template <class T> bool removeComponent() {
        auto component = getComponent<T>();
        return component && removeComponent(component->assetId);
    }
    std::shared_ptr<Component> GetComponent(std::string) const;
    template <class T> std::shared_ptr<T> getComponent() const {
        checkLogicThread();
        auto found = types_.find(std::type_index(typeid(T)));
        if (found != types_.end())
            return std::static_pointer_cast<T>(found->second);
        // Base-type queries are a deterministic compatibility path.
        for (const auto &entry : components_)
            if (auto value = std::dynamic_pointer_cast<T>(entry.second))
                return value;
        return {};
    }
    virtual void loadFromJson(json &);
    bool isDeferred() const;
    void setDeferred(bool);
    void checkLogicThread() const;
    void sealForTransfer(); // Detached producer freezes APIs before delivering a future result.
    uint64_t componentRevision() const {
        checkLogicThread();
        return componentRevision_;
    }
    std::string name; // Legacy editor label; only the logic thread writes it.
  private:
    friend class RenderScene;
    void bindScene(const std::shared_ptr<RenderScene> &, const RenderScene *previous = nullptr);
    void unbindScene(const RenderScene *);
    void componentsChanged();
    void transferAssets(bool seal);
    void sealForTransfer(const RenderScene *);
    std::map<std::string, std::shared_ptr<Component>> components_;
    std::unordered_map<std::type_index, std::shared_ptr<Component>> types_;
    std::weak_ptr<RenderScene> scene_;
    std::thread::id logicThread_ = std::this_thread::get_id();
    uint64_t componentRevision_ = 0;
    bool deferred_ = true;
    bool sealed_ = false;
};
