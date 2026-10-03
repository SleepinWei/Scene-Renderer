#include "engine/AssetPath.h"
#include "system/ResourceManager.h"
#include "renderer/Texture.h"
#include "rhi/Device.h"
#include <filesystem>
namespace {
std::string key(const std::string &path) {
    return engine::AssetPath::resolve(path);
}
std::shared_ptr<Texture> decode(const std::string &path, bool gpu) {
    auto texture = gpu ? Texture::loadFromFile(path) : Texture::loadFromFileAsync(path);
    if (!texture || texture->getWidth() <= 0 || texture->getHeight() <= 0 || (!texture->pixels() && !texture->gpuId()))
        throw std::runtime_error("Cannot decode texture: " + path);
    return texture;
}
} // namespace
std::shared_ptr<Texture> ResourceManager::find(std::string path) {
    auto k = key(path);
    auto value = cpu_.find(k);
    return value ? value : legacyGpu_.find(k);
}
std::shared_ptr<Texture> ResourceManager::getResource(const std::string &path) {
    auto k = key(path);
    if (rhi::usesNativeRenderer())
        return cpu_.get(k, [&] { return decode(k, false); });
    return legacyGpu_.get(k, [&] { return decode(k, true); });
}
std::shared_ptr<Texture> ResourceManager::getResourceAsync(const std::string &path) {
    auto k = key(path);
    return cpu_.get(k, [&] { return decode(k, false); });
}
