#include "renderer/rhi/GpuMaterial.h"
// Keep decode orientation local to the RHI asset path. The legacy global STB
// decoder flips its inputs and may run concurrently on an asset thread.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stdexcept>
#include <filesystem>
namespace render {
ImageRGBA8 decodeDds(const std::string&);
ImageRGBA8 ImageRGBA8::load(const std::string& path) {
    if(std::filesystem::path(path).extension()==".dds" || std::filesystem::path(path).extension()==".DDS")return decodeDds(path);
    int width = 0, height = 0, channels = 0;auto* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) throw std::runtime_error("Renderer: cannot decode texture " + path);
    try { ImageRGBA8 result{uint32_t(width), uint32_t(height), std::vector<uint8_t>(data, data + size_t(width) * height * 4)};stbi_image_free(data);return result; }
    catch (...) { stbi_image_free(data);throw; }
}
}
