#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/GpuVirtualTexture.h"
#include <optional>
#include "component/VegetationSettings.h"
#include "engine/Environment.h"
#include "renderer/rhi/TreeWind.h"
class RenderScene;
namespace render {
struct MeshPayload {
    uint64_t id = 0, revision = 0;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<glm::vec4> pathTracingTangents; // Optional corner tangent + handedness, matching vertices; shared by PT and native Raster.
    std::vector<TreeWindVertex> treeWind;
};
struct ImageRGBA32F {
    uint32_t width=0,height=0;
    std::vector<float> pixels; // Linear RGBA, native FFT row order.
};
struct OceanNormalFields {
    std::array<std::shared_ptr<const ImageRGBA32F>,2> images;
    glm::vec2 scales{1}; // Surface length / main and detail periods.
};
struct MaterialPayload {
    uint64_t id = 0, revision = 0;
    std::array<std::shared_ptr<const ImageRGBA8>, 5> images;
    std::shared_ptr<const ImageRGBA8> special;
    std::shared_ptr<const ImageRGBA8> height;
    // Detached full-precision FFT fields for PT. Sample each native period
    // before combining slopes; do not quantize or bake detail into the main map.
    std::shared_ptr<const OceanNormalFields> oceanNormals;
};
struct SnapshotDraw {
    uint64_t objectId = 0;
    uint32_t shading = 0;
    std::shared_ptr<const MeshPayload> mesh;
    std::shared_ptr<const MaterialPayload> material;
    MaterialParameters parameters;
    MaterialExtension extension;
    glm::mat4 model{1};
    bool subdivision = false, wireframe = false;
    // Detached PT optics for frozen geometry. GGX reflection fields also bridge to native Raster.
    float pathTracingIor = 0;
    float pathTracingRoughness = 0; // Dielectric boundary only; zero is delta.
    uint32_t pathTracingBsdfModel = 0; // 0 PBR, 1 Lambert, 2 GGX + Lambert, 3 thin diffuse reflection/transmission, 4 cloth, 5 hair ribbon.
    glm::vec4 pathTracingFiber{.3f,.3f,1.55f,.034906585f}; // Hair beta_m, beta_n, IOR, scale tilt radians.
    glm::vec4 pathTracingSheen{.5f,.5f,0,0}; // Cloth sheen weight/roughness, thin transmission, yarn specular weight.
    glm::vec3 pathTracingSheenColor{1};
    glm::vec4 pathTracingKnit{0,24,32,.12f}; // Cloth stitch weight, repeats U/V, yarn radius in a stitch cell.
    glm::vec3 pathTracingDiffuseTransmission{0}; // Linear RGB; no dielectric medium transition.
    std::shared_ptr<const ImageRGBA8> pathTracingTransmissionTexture;
    bool pathTracingRoughnessTexture = false; // Linear G channel of images[3].
    glm::vec3 pathTracingAbsorption{0},pathTracingScattering{0};
    float pathTracingSpecularAnisotropy=0, pathTracingSpecularRotation=0; // GGX axes, [-.9,.9] and radians; independent of volume HG g.
    float pathTracingAnisotropy=0;
    float pathTracingNormalScale = 1;
    uint32_t pathTracingKind = 0; // 1 terrain, 2 grass, 3 ocean interface, 4 subsurface solid, 5 smooth thin dielectric.
    // Level-zero repeating beach maps and clamped terrain-UV shoreline mask.
    std::array<std::shared_ptr<const ImageRGBA8>,4> pathTracingShoreline;
};
struct TerrainPayload {
    uint64_t id = 0, revision = 0;
    uint32_t capacity = 2048,virtualColumns=8;
    VirtualTextureSource height, material;
    bool grass = false;
    VegetationSettings vegetation;
    std::shared_ptr<const ImageRGBA8> waterMask;
    std::array<std::shared_ptr<const ImageRGBA8>,4> shorelineImages;
    std::shared_ptr<const WaterBathymetry> bathymetry;
};
struct SnapshotTerrain {
    std::optional<VegetationSettings> vegetation;
    std::shared_ptr<const TerrainPayload> source;
    glm::mat4 model{1};
    MaterialParameters parameters;
    bool wireframe = false;
    MaterialExtension extension;
};
// Contains values and const, detached CPU payloads; no GameObject, Component,
// Camera, Material or GPU handle crosses the logic/render boundary.
struct RenderWorldSnapshot {
    std::optional<engine::EnvironmentSnapshot> environment;
    uint64_t sequence = 0;
    bool asynchronousStreaming = false,automaticQuality=false;
    bool rasterFrustumCulling=false; // Main-view static instances only; retain auxiliary geometry.
    bool rasterInstanceBatching=false; // PT preview may reorder opaque static draws.
    FrameData frame;
    float exposure = 1;
    std::vector<SnapshotDraw> draws;
    std::optional<SnapshotTerrain> terrain;
};
class SceneSnapshotBuilder {
  public:
    SceneSnapshotBuilder();
    ~SceneSnapshotBuilder();
    // Logic-thread only. Nonblocking capture returns null while new assets are
    // decoding; editor continues events and last submitted render remains visible.
    std::shared_ptr<const RenderWorldSnapshot> capture(const std::shared_ptr<RenderScene> &, float time,
                                                       uint32_t width, uint32_t height,
                                                       bool waitForAssets = true);
    void invalidateAssets();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace render
