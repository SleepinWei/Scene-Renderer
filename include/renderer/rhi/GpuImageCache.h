#pragma once
#include "renderer/rhi/GpuMaterial.h"
#include <unordered_map>

namespace render {
// GPU leases are released on the owning device thread, after material binding sets.
class GpuImage {
  public:
    GpuImage(std::shared_ptr<rhi::GraphicsDevice>, std::shared_ptr<const ImageRGBA8>);
    rhi::TextureViewHandle view() const { return view_; }
    rhi::TextureHandle texture() const { return texture_; }
    const ImageRGBA8 &source() const { return *source_; }
    size_t bytes() const { return source_->pixels.size(); }

  private:
    std::shared_ptr<const ImageRGBA8> source_;
    Resources resources_;
    rhi::TextureHandle texture_;
    rhi::TextureViewHandle view_;
};
struct GpuImageCacheStats {
    size_t residentBytes = 0, idleBytes = 0, entries = 0;
    uint64_t uploads = 0, uploadedBytes = 0, hits = 0, evictions = 0;
};
// One cache per device. RGBA8UNorm content is independent of material slot and
// sampler. A hash is only an index: exact dimensions and bytes confirm equality.
class GpuImageCache {
  public:
    static std::shared_ptr<GpuImageCache> forDevice(std::shared_ptr<rhi::GraphicsDevice>);
    std::shared_ptr<GpuImage> acquire(std::shared_ptr<const ImageRGBA8>);
    size_t missingBytes(const std::vector<std::shared_ptr<const ImageRGBA8>> &) const;
    void setIdleBudget(size_t bytes);
    void trim();
    GpuImageCacheStats stats() const;

  private:
    explicit GpuImageCache(std::shared_ptr<rhi::GraphicsDevice>);
    struct Entry {
        std::shared_ptr<GpuImage> gpu;
        uint64_t touched = 0;
    };
    const Entry *find(const ImageRGBA8 &) const;
    std::shared_ptr<rhi::GraphicsDevice> device_;
    std::unordered_multimap<uint64_t, Entry> entries_;
    // Immutable shared sources give repeated file images a constant-time path.
    struct Identity {
        std::weak_ptr<const ImageRGBA8> source;
        std::weak_ptr<GpuImage> gpu;
        uint64_t hash = 0;
    };
    std::unordered_map<const ImageRGBA8 *, Identity> identities_;
    size_t idleBudget_ = 64 * 1024 * 1024;
    uint64_t clock_ = 0, uploads_ = 0, uploadedBytes_ = 0, hits_ = 0, evictions_ = 0;
};
} // namespace render
