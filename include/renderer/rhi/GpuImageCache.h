#pragma once
#include "renderer/rhi/GpuMaterial.h"
#include "renderer/rhi/ImageMipChain.h"
#include <unordered_map>

namespace render {
// GPU leases are released on the owning device thread, after material binding sets.
class GpuImage {
  public:
    GpuImage(std::shared_ptr<rhi::GraphicsDevice>, std::shared_ptr<const ImageRGBA8>,bool deferred=false,ImageMipPolicy policy={});
    bool ready() const {return uploadMip_>mips_.size();}
    size_t uploadRows(size_t maxBytes);
    rhi::TextureViewHandle view() const { return view_; }
    rhi::TextureHandle texture() const { return texture_; }
    const ImageRGBA8 &source() const { return *source_; }
    size_t bytes() const { return bytes_; }
    ImageMipPolicy policy()const{return policy_;}
    uint32_t mipLevels()const{return uint32_t(mips_.size()+1);}

  private:
    std::shared_ptr<const ImageRGBA8> source_;
    Resources resources_;
    rhi::TextureHandle texture_;
    rhi::TextureViewHandle view_;
    uint32_t nextRow_=0;
    uint32_t uploadMip_=0;
    size_t bytes_=0;
    ImageMipPolicy policy_;
    std::vector<ImageRGBA8> mips_;
};
struct GpuImageCacheStats {
    size_t residentBytes = 0, idleBytes = 0, entries = 0, pendingEntries=0;
    uint64_t uploads = 0, uploadedBytes = 0, hits = 0, evictions = 0;
};
struct GpuImageSource {std::shared_ptr<const ImageRGBA8> image;ImageMipPolicy policy;};
// One cache per device. Content and mip reduction policy identify an image;
// sampler choice remains independent. Exact bytes confirm hash matches.
class GpuImageCache {
  public:
    static std::shared_ptr<GpuImageCache> forDevice(std::shared_ptr<rhi::GraphicsDevice>);
    std::shared_ptr<GpuImage> acquire(std::shared_ptr<const ImageRGBA8>,bool deferred=false,ImageMipPolicy policy={});
    size_t upload(const std::shared_ptr<GpuImage>&,size_t maxBytes);
    size_t missingBytes(const std::vector<std::shared_ptr<const ImageRGBA8>> &) const;
    size_t missingFilteredBytes(const std::vector<GpuImageSource>&)const;
    void setIdleBudget(size_t bytes);
    void trim();
    size_t releaseIdle(size_t bytes = SIZE_MAX); // Never evicts a live material lease.
    GpuImageCacheStats stats() const;

  private:
    explicit GpuImageCache(std::shared_ptr<rhi::GraphicsDevice>);
    struct Entry {
        std::shared_ptr<GpuImage> gpu;
        uint64_t touched = 0;
    };
    const Entry *find(const ImageRGBA8 &,ImageMipPolicy={}) const;
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
