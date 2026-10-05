#pragma once
#include "renderer/rhi/SceneSnapshot.h"
namespace pt {
// Explicit std430 ABI shared by CPU export and the portable GPU kernel.
struct alignas(16) PackedVertex {glm::vec4 positionU, normalV;};
struct alignas(16) PackedNode {glm::vec4 low, high;}; // w stores uint bits: first/count.
struct alignas(16) PackedMaterial {
    glm::vec4 albedo, emission, factors;
    glm::vec4 optics{0}; // x: dielectric IOR; zero uses basic PBR.
    glm::vec4 absorption{0},scattering{0}; // absorption.w: dielectric roughness; scattering.w: HG g.
    glm::uvec4 textures, extra; // extra: AO image, two-sided, emissive-albedo.
};
struct alignas(16) PackedEmitter {glm::vec4 primitiveAreaCDF, normal;};
struct SceneData {
    std::vector<PackedVertex> vertices;
    std::vector<PackedNode> nodes;
    std::vector<glm::uvec4> triangles, images;
    std::vector<uint32_t> texels;
    std::vector<PackedMaterial> materials;
    std::vector<render::LightData> lights;
    std::vector<PackedEmitter> emitters;
    glm::mat4 inverseProjection{1};
    glm::vec3 camera{0};
    std::array<glm::uvec4,2> cameraMedia{},cameraWinding{};
    double emitterWeight=0;
    bool inverseSquare=true;
};
} // namespace pt
