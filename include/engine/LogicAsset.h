#pragma once
#include "engine/AssetIdentity.h"
#include <thread>
#include <stdexcept>
class GameObject;
class RenderScene;
namespace engine {
// Mutable CPU assets travel with a sealed world. Worker payloads are separate values.
class LogicAsset : public AssetIdentity {
  public:
    LogicAsset() = default;
    LogicAsset(const LogicAsset &other) : AssetIdentity(checked(other)) {}
    LogicAsset &operator=(const LogicAsset &) = delete;
    void checkLogicThread() const {
        if (sealed_ || thread_ != std::this_thread::get_id())
            throw std::logic_error("CPU asset is sealed or accessed outside its logic thread");
    }
    uint64_t getContentRevision() const {
        checkLogicThread();
        return AssetIdentity::getContentRevision();
    }
    void invalidate() {
        checkLogicThread();
        AssetIdentity::invalidate();
    }

  private:
    friend class ::GameObject;
    friend class ::RenderScene;
    static const LogicAsset &checked(const LogicAsset &other) {
        other.checkLogicThread();
        return other;
    }
    void sealOwnership() {
        if (sealed_)
            return;
        checkLogicThread();
        sealed_ = true;
    }
    void adoptOwnership() {
        if (!sealed_ && thread_ != std::this_thread::get_id())
            throw std::logic_error("Foreign CPU asset must be sealed before handoff");
        thread_ = std::this_thread::get_id();
        sealed_ = false;
    }
    bool sealed_ = false;
    std::thread::id thread_ = std::this_thread::get_id();
};
} // namespace engine
