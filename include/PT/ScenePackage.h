#pragma once
#include "PT/CpuPathTracer.h"
#include <json/json.hpp>
namespace pt {
// Frozen, versioned interchange; Blender is needed only during offline export.
struct ScenePackage {
    render::RenderWorldSnapshot snapshot;
    std::shared_ptr<Environment> environment;
    glm::vec3 sunDirection{0,1,0},sunIrradiance{0};
    float sunRadius=0;
    nlohmann::json metadata;
};
ScenePackage loadScenePackage(const std::string &path,uint32_t width,uint32_t height);
}
