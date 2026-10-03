#pragma once
#include "engine/AssetIdentity.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <stdexcept>
#include <thread>
#include <json/json.hpp>
using json = nlohmann::json;
class GameObject;
class Component : public engine::AssetIdentity {
  public:
    Component() = default;
    Component(const Component &) = delete;
    Component &operator=(const Component &) = delete;
    virtual ~Component() = default;
    std::shared_ptr<GameObject> owner() const;
    bool hasOwner() const;
    void checkLogicThread() const;
    uint64_t getContentRevision() const {checkLogicThread();return engine::AssetIdentity::getContentRevision();}
    void invalidate() {
        checkLogicThread();
        engine::AssetIdentity::invalidate();
    }
    const std::string &typeName() const { return name; }
    virtual void loadFromJson(json &data) {};

  protected:
    std::string name; // Reflection name established by concrete constructors.
  private:
    friend class GameObject;
    std::weak_ptr<GameObject> owner_;
    std::thread::id logicThread_ = std::this_thread::get_id();
};
