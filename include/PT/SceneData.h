#pragma once
#include "renderer/rhi/SceneSnapshot.h"
namespace pt {
// Explicit std430 ABI shared by CPU export and the portable GPU kernel.
struct alignas(16) PackedVertex {glm::vec4 positionU, normalV,tangent{0};};
struct alignas(16) PackedNode {glm::vec4 low, high;}; // w stores uint bits: first/count.
struct alignas(16) PackedInstance {
    glm::mat4 model,inverse;
    glm::uvec4 info; // BLAS root, material/medium index, primitive base, triangle base.
    glm::uvec4 range; // triangle count, mirrored winding, geometry index, exported tangent frame flag.
};
struct alignas(16) PackedMaterial {
    glm::vec4 albedo, emission, factors;
    glm::vec4 optics{0}; // x: dielectric IOR; y: FFT water; z: FFT normal scale; w: closed-boundary/roughness-texture/opaque-shadow bits (1/2/4).
    glm::vec4 absorption{0},scattering{0}; // absorption.w: dielectric roughness; scattering.w: HG g.
    glm::uvec4 textures, extra; // extra: AO image, two-sided, emissive-albedo, thin dielectric bit + BSDF model in bits 1–2.
    glm::vec4 shoreHeight{0},shoreSurface{0};
    glm::uvec4 shoreTextures{UINT32_MAX}; // beach albedo/normal/ORM, shoreline mask; disabled when x is UINT32_MAX.
    glm::vec4 diffuseTransmission{0}; // Linear RGB; w stores transmission image ID bits, used only by BSDF model 3.
};
struct alignas(16) PackedEmitter {glm::vec4 primitiveAreaCDF, normal;};
struct SceneData {
    std::vector<PackedVertex> vertices;
    std::vector<PackedNode> nodes;
    std::vector<PackedInstance> instances;
    std::vector<uint32_t> instanceOrder;
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
