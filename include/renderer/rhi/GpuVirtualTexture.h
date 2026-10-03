#pragma once
#include "renderer/rhi/Resources.h"
#include "renderer/rhi/GpuMaterial.h"
#include <functional>
#include <map>
#include <tuple>
namespace render {
// Pages are tightly packed, including a two-texel apron. Sources may be memory,
// procedural, or disk backed; only the disk source avoids full CPU residency.
struct VirtualTextureSource {
    uint32_t extent = 64;
    float minimum = 0, maximum = 0;
    bool heightField = false;
    std::vector<rhi::Format> formats;
    using Page = std::vector<std::vector<uint8_t>>;
    std::function<Page(uint32_t mip, uint32_t x, uint32_t y)> readPage;
};
VirtualTextureSource heightVirtualSource(uint32_t width, uint32_t height, std::vector<float>);
VirtualTextureSource rawHeightVirtualSource(const std::string &, uint32_t width, uint32_t height);
VirtualTextureSource materialVirtualSource(const std::array<ImageRGBA8, 5> &);
VirtualTextureSource packedVirtualSource(const std::string &manifest);
class GpuVirtualTexture {
  public:
    static constexpr uint32_t Tile = 64, Border = 2, Pitch = Tile + 2 * Border;
    struct PageId {
        uint32_t mip, x, y;
        bool operator<(const PageId &b) const { return std::tie(mip, x, y) < std::tie(b.mip, b.x, b.y); }
        bool operator==(const PageId &b) const { return mip == b.mip && x == b.x && y == b.y; }
    };
    GpuVirtualTexture(std::shared_ptr<rhi::GraphicsDevice>, VirtualTextureSource, uint32_t columns = 8);
    // CPU conservative visibility requests, capped before upload. No synchronous GPU readback.
    void prepare(const glm::mat4 &viewProjection, const glm::mat4 &model, uint32_t viewportWidth,
                 uint32_t viewportHeight, bool flipV = false, uint32_t uploads = 8);
    void update(const std::vector<PageId> &requested, uint32_t uploads = 8);
    rhi::TextureViewHandle atlas(uint32_t layer = 0) const { return atlasViews_.at(layer); }
    rhi::TextureViewHandle pageTable() const { return tableView_; }
    rhi::SamplerHandle sampler() const { return sampler_; }
    rhi::TextureHandle atlasTexture(uint32_t layer = 0) const { return atlases_.at(layer); }
    rhi::TextureHandle tableTexture() const { return table_; }
    uint32_t extent() const { return source_.extent; }
    uint32_t maxMip() const { return maxMip_; }
    uint32_t layers() const { return uint32_t(source_.formats.size()); }
    bool heightField() const { return source_.heightField; }
    uint32_t capacity() const { return columns_ * columns_; }
    uint32_t residentPages() const { return uint32_t(resident_.size()); }
    uint64_t version() const { return version_; }
    size_t physicalBytes() const;
    float minimum() const { return source_.minimum; }
    float maximum() const { return source_.maximum; }

  private:
    void install(PageId, uint32_t slot, VirtualTextureSource::Page pages = {});
    void uploadTable();
    uint32_t row(uint32_t mip) const;
    VirtualTextureSource source_;
    Resources resources_;
    uint32_t columns_, maxMip_, tableWidth_, tableRows_;
    uint64_t clock_ = 0, version_ = 0;
    struct Resident {
        uint32_t slot;
        uint64_t touched;
    };
    std::map<PageId, Resident> resident_;
    std::vector<float> tableData_;
    std::vector<rhi::TextureHandle> atlases_;
    std::vector<rhi::TextureViewHandle> atlasViews_;
    rhi::TextureHandle table_;
    rhi::TextureViewHandle tableView_;
    rhi::SamplerHandle sampler_;
};
void validateVirtualTextureRhi(std::shared_ptr<rhi::GraphicsDevice>, const std::string &shaderDirectory);
} // namespace render
