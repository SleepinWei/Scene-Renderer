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
    glm::uvec4 textures, extra; // extra: AO image, two-sided, emissive-albedo, thin dielectric bit + BSDF model above bit 0.
    glm::vec4 shoreHeight{0},shoreSurface{0};
    glm::uvec4 shoreTextures{UINT32_MAX}; // beach albedo/normal/ORM, shoreline mask; disabled when x is UINT32_MAX.
    glm::vec4 specularShape{0}; // anisotropy, rotation radians, rain image index + 1 (0 absent), uniform rain wetness.
    glm::vec4 fiber{.3f,.3f,1.55f,.034906585f},sheen{.5f,.5f,0,0},sheenColor{1};
    glm::vec4 knit{0,24,32,.12f};
    glm::vec4 diffuseTransmission{0}; // Linear RGB; w stores transmission image ID bits, used only by BSDF model 3.
    glm::uvec4 oceanNormals{UINT32_MAX,UINT32_MAX,0,0}; // Main/detail IDs, float bits of their UV scales.
};
struct alignas(16) PackedEmitter {glm::vec4 primitiveAreaCDF, normal;};
struct SceneCamera {
    glm::mat4 inverseProjection{1};
    glm::vec3 position{0};
    std::array<glm::uvec4,2> media{},winding{};
};
// Immutable packed storage shared by exports and adjacent CpuScene snapshots.
// Roots and triangle bases follow the ordered unique geometry list.
struct PackedGeometry {
    std::vector<PackedVertex> vertices;
    std::vector<PackedNode> blasNodes;
    std::vector<glm::uvec4> triangles;
    std::vector<uint32_t> roots,triangleBases;
};
struct PackedTextures {
    std::vector<glm::uvec4> images;
    std::vector<uint32_t> texels;
    // Keep identity owners alive; pointer reuse cannot impersonate an old image.
    std::vector<std::shared_ptr<const render::ImageRGBA8>> sources;
    // Parallel to sources: exactly one owner per descriptor. images.w=1 uses
    // four float-bit words per texel; images.w=0 retains packed UNORM8.
    std::vector<std::shared_ptr<const render::ImageRGBA32F>> floatSources;
};
struct SceneData {
    std::shared_ptr<const PackedGeometry> geometry;
    std::shared_ptr<const PackedTextures> textures;
    bool geometryReused=false,texturesReused=false;
    // Independent address spaces: instance.info.x indexes geometry->blasNodes; TLAS
    // child addresses index tlasNodes. Transform updates never modify BLAS.
    std::vector<PackedNode> tlasNodes;
    std::vector<PackedInstance> instances;
    std::vector<uint32_t> instanceOrder;
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
