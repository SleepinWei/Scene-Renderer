#include "renderer/rhi/GpuMaterial.h"
// Keep decode orientation local to the RHI asset path. The legacy global STB
// decoder flips its inputs and may run concurrently on an asset thread.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stdexcept>
#include <filesystem>
#include "engine/AssetCache.h"
namespace render {
namespace {engine::AssetCache<ImageRGBA8> cache;}
ImageRGBA8 decodeDds(const std::string&);
ImageRGBA8 ImageRGBA8::load(const std::string& path) {
    if(std::filesystem::path(path).extension()==".dds" || std::filesystem::path(path).extension()==".DDS")return decodeDds(path);
    int width = 0, height = 0, channels = 0;auto* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) throw std::runtime_error("Renderer: cannot decode texture " + path);
    try { ImageRGBA8 result{uint32_t(width), uint32_t(height), std::vector<uint8_t>(data, data + size_t(width) * height * 4)};stbi_image_free(data);return result; }
    catch (...) { stbi_image_free(data);throw; }
}
}

namespace render {
std::shared_ptr<const ImageRGBA8> ImageRGBA8::loadShared(const std::string& path){
    auto file=std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    auto key=file.generic_string()+"|"+std::to_string(static_cast<long long>(std::filesystem::last_write_time(file).time_since_epoch().count()))+"|"+std::to_string(std::filesystem::file_size(file));
    return cache.get(key,[&]{return std::make_shared<ImageRGBA8>(load(file.string()));});
}
size_t ImageRGBA8::releaseUnused(){return cache.releaseUnused();}
}
