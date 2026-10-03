#pragma once
#include "engine/AssetCache.h"
class Texture;
class ResourceManager {
  public:
    static ResourceManager *GetInstance() {
        static ResourceManager manager;
        return &manager;
    }
    std::shared_ptr<Texture> find(std::string path);
    std::shared_ptr<Texture> getResource(const std::string &path);
    // Compatibility name: CPU decode on caller; Loader::buildScene is asynchronous.
    std::shared_ptr<Texture> getResourceAsync(const std::string &path);
    size_t releaseUnused() { return cpu_.releaseUnused() + legacyGpu_.releaseUnused(); }

  private:
    ResourceManager() = default;
    engine::AssetCache<Texture> cpu_, legacyGpu_;
};
