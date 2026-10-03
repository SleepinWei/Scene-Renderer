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
enum class MaterialFeature : int { VirtualTexture = 1 };
struct alignas(16) MaterialExtension {
    glm::vec4 lobes{0, .5f, 0, 0};
    glm::vec4 settings{.1f, .05f, 0, 0};
    glm::ivec4 features{0};
};
struct MaterialDesc {
    MaterialParameters parameters;
    // albedo (encoded RGB), normal (linear), metallic.b, roughness.g, AO.r.
    std::array<ImageRGBA8, 5> images;
    // Immutable snapshot sources avoid copying images when creating a GPU material.
    // A shared source takes precedence over the corresponding value image.
    std::array<std::shared_ptr<const ImageRGBA8>, 5> sharedImages;
    std::shared_ptr<const ImageRGBA8> sharedSpecial;
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
    rhi::BindingSetHandle bindings_, shadowBindings_, rsmBindings_;
};
} // namespace render
