#pragma once
#include "rhi/ShaderAssets.h"
#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <vector>
namespace rhi {
inline std::filesystem::path pipelineDiskPath(const std::string& identity) {
    const auto disabled=std::getenv("SCENERENDERER_DISABLE_PIPELINE_DISK_CACHE");
    if(disabled && std::string(disabled)=="1")return {};
    return std::filesystem::path(defaultShaderDirectory())/"native-cache"/identity;
}
inline std::vector<uint8_t> readPipelineDisk(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return {};
    auto size=file.tellg();if(size<=0 || size>64*1024*1024)return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),size);
    if(!file)return {};return bytes;
}
inline std::filesystem::path pipelineDiskTemporary(const std::filesystem::path& path) {
    static std::atomic<uint64_t> serial{0};
    return path.string()+".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);
}
inline bool writePipelineDisk(const std::filesystem::path& path,const std::vector<uint8_t>& bytes) noexcept {
    if(path.empty() || bytes.empty() || bytes.size()>64*1024*1024)return false;
    std::filesystem::path temporary;
    try {
        std::filesystem::create_directories(path.parent_path());temporary=pipelineDiskTemporary(path);
        {std::ofstream file(temporary,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!file)throw std::runtime_error("Cache write failed");}
        std::filesystem::rename(temporary,path);return true;
    } catch(...) {std::error_code error;std::filesystem::remove(temporary,error);return false;}
}
}
