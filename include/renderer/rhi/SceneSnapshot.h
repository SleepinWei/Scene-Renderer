#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/GpuVirtualTexture.h"
#include <optional>
class RenderScene;
namespace render {
struct MeshPayload {
    uint64_t id = 0, revision = 0;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};
struct MaterialPayload {
    uint64_t id = 0, revision = 0;
    std::array<std::shared_ptr<const ImageRGBA8>, 5> images;
    std::shared_ptr<const ImageRGBA8> special;
    std::shared_ptr<const ImageRGBA8> height;
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
};
struct TerrainPayload {
    uint64_t id = 0, revision = 0;
    uint32_t capacity = 2048;
    VirtualTextureSource height, material;
    bool grass = false;
};
struct SnapshotTerrain {
    std::shared_ptr<const TerrainPayload> source;
    glm::mat4 model{1};
    MaterialParameters parameters;
    bool wireframe = false;
};
// Contains values and const, detached CPU payloads; no GameObject, Component,
// Camera, Material or GPU handle crosses the logic/render boundary.
struct RenderWorldSnapshot {
    uint64_t sequence = 0;
    bool asynchronousStreaming = false;
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
