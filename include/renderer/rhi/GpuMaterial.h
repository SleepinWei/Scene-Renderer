#pragma once
#include "renderer/rhi/Resources.h"
#include <glm/glm.hpp>
#include <array>
namespace render {
struct ImageRGBA8 {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels; // row zero at top, linear data or encoded albedo.
    static ImageRGBA8 load(const std::string &path);
    static std::shared_ptr<const ImageRGBA8> loadShared(const std::string &path);
    static size_t releaseUnused();
};
struct alignas(16) MaterialParameters {
    glm::vec4 albedoAlpha{1};
    glm::vec4 factors{1, 1, 1, 0};        // metallic, roughness, AO strength, alpha cutoff.
    glm::vec4 emissiveNormal{0, 0, 0, 1}; // emissive RGB, normal strength.
};
enum class MaterialFeature : int { VirtualTexture = 1, Shoreline = 2, RainWetness = 4, SpatialRainWetness = 8, FilteredNormals = 16, Foliage = 32 };
struct alignas(16) MaterialExtension {
    glm::vec4 lobes{0, .5f, 0, 0};
    glm::vec4 settings{.1f, .05f, 0, 0};
    glm::ivec4 features{0}; // x feature bits, y rain wetness UNORM16; z/w reserved.
    glm::vec4 shoreHeight{0,24,1,3}, shoreSurface{30,.7f,.95f,1};
    // x: GGX anisotropy, y: radians, z: 0 legacy / 1 PT PBR / 2 PT GGX-add.
    glm::vec4 specularShape{0};
    // 16x16 cell-centred UNORM8 rain wetness in material UV orientation.
    // Four cells per uint; this reuses the material UBO without another sampler.
    std::array<glm::uvec4,16> rainWetCells{};
};
struct MaterialDesc {
    MaterialParameters parameters;
    // albedo (encoded RGB), normal (linear), metallic.b, roughness.g, AO.r.
    std::array<ImageRGBA8, 5> images;
    // Immutable snapshot sources avoid copying images when creating a GPU material.
    // A shared source takes precedence over the corresponding value image.
    std::array<std::shared_ptr<const ImageRGBA8>, 5> sharedImages;
    std::shared_ptr<const ImageRGBA8> sharedSpecial;
    std::array<std::shared_ptr<const ImageRGBA8>,4> shorelineImages;
    MaterialExtension extension;
    ImageRGBA8 special;
    bool transparent = false;
    rhi::Filter filter = rhi::Filter::Linear;
};
class GpuVirtualTexture;
class GpuImage;
class GpuImageCache;
class GpuMaterial {
  public:
    GpuMaterial(std::shared_ptr<rhi::GraphicsDevice>, const MaterialDesc &,
                std::shared_ptr<GpuVirtualTexture> virtualTexture = {});
    static size_t imageUploadBytes(std::shared_ptr<rhi::GraphicsDevice>, const MaterialDesc &);
    void update(const MaterialParameters &);
    void updateExtension(const MaterialExtension &);
    const MaterialExtension &extension() const { return extensionData_; }
    const MaterialParameters &parameters() const { return parameterData_; }
    bool transparent() const {
        return transparent_ || (parameterData_.albedoAlpha.w < .999f && parameterData_.factors.w == 0);
    }
    void bind(rhi::CommandList &) const;
    void bindRsm(rhi::CommandList &) const;
    static rhi::BindingLayout rsmLayout();
    void bindShadow(rhi::CommandList &) const;
    static rhi::BindingLayout shadowLayout();
    const rhi::GraphicsDevice *owner() const { return resources_.device.get(); }
    static rhi::BindingLayout layout();

  private:
    std::shared_ptr<GpuVirtualTexture> virtualTexture_;
    std::shared_ptr<GpuImageCache> imageCache_;
    std::vector<std::shared_ptr<GpuImage>> images_; // Binding sets retire before these leases.
    Resources resources_;
    rhi::BufferHandle parameters_, extension_;
    MaterialParameters parameterData_;
    MaterialExtension extensionData_;
    bool transparent_ = false;
    bool shoreline_ = false;
    bool filteredNormals_ = false;
    rhi::BindingSetHandle bindings_, shadowBindings_, rsmBindings_;
};
} // namespace render
