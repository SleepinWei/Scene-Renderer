#include "renderer/rhi/GpuImageCache.h"
#include <mutex>
#include <algorithm>
#include <map>
namespace render {
namespace {
void validate(const ImageRGBA8 &image) {
    if (!image.width || !image.height || uint64_t(image.width) * image.height > SIZE_MAX / 4 ||
        image.pixels.size() != size_t(image.width) * image.height * 4)
        throw std::invalid_argument("GPU image cache requires complete RGBA8 data");
}
uint64_t hashImage(const ImageRGBA8 &image) {
    uint64_t hash = 14695981039346656037ull;
    for (auto byte : image.pixels) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash ^ (uint64_t(image.width) << 32) ^ image.height;
}
bool equalImage(const ImageRGBA8 &a, const ImageRGBA8 &b) {
    return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
}
} // namespace
GpuImage::GpuImage(std::shared_ptr<rhi::GraphicsDevice> device, std::shared_ptr<const ImageRGBA8> source)
    : source_(std::move(source)), resources_(std::move(device)) {
    validate(*source_);
    texture_ = resources_.texture(
        {source_->width, source_->height, rhi::Format::RGBA8UNorm,
         rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination | rhi::TextureUsage::CopySource,
         "Shared PBR image"});
    resources_.device->writeTexture(texture_, source_->pixels.data(), source_->pixels.size());
    view_ = resources_.view(texture_);
}
GpuImageCache::GpuImageCache(std::shared_ptr<rhi::GraphicsDevice> device) : device_(std::move(device)) {}
std::shared_ptr<GpuImageCache> GpuImageCache::forDevice(std::shared_ptr<rhi::GraphicsDevice> device) {
    if (!device || !device->isOpen())
        throw std::invalid_argument("Image cache needs an open device");
    device->checkThread();
    static std::mutex mutex;
    static std::map<const rhi::GraphicsDevice *, std::weak_ptr<GpuImageCache>> registry;
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = registry.begin(); it != registry.end();)
        if (it->second.expired())
            it = registry.erase(it);
        else
            ++it;
    auto &cached = registry[device.get()];
    auto result = cached.lock();
    if (!result) {
        result = std::shared_ptr<GpuImageCache>(new GpuImageCache(std::move(device)));
        cached = result;
    }
    return result;
}
const GpuImageCache::Entry *GpuImageCache::find(const ImageRGBA8 &image) const {
    auto range = entries_.equal_range(hashImage(image));
    for (auto it = range.first; it != range.second; ++it)
        if (equalImage(it->second.gpu->source(), image))
            return &it->second;
    return nullptr;
}
std::shared_ptr<GpuImage> GpuImageCache::acquire(std::shared_ptr<const ImageRGBA8> image) {
    device_->checkThread();
    if (!image)
        throw std::invalid_argument("Null GPU image source");
    validate(*image);
    // Fast path requires a still-live immutable source, not just a reused address.
    auto identity = identities_.find(image.get());
    if (identity != identities_.end() && identity->second.source.lock() == image)
        if (auto gpu = identity->second.gpu.lock()) {
            auto range = entries_.equal_range(identity->second.hash);
            for (auto it = range.first; it != range.second; ++it)
                if (it->second.gpu == gpu) {
                    it->second.touched = ++clock_;
                    ++hits_;
                    return gpu;
                }
        }
    if (auto found = find(*image)) {
        auto gpu = found->gpu;
        const_cast<Entry *>(found)->touched = ++clock_;
        identities_[image.get()] = {image, gpu, hashImage(*image)};
        ++hits_;
        return gpu;
    }
    auto gpu = std::make_shared<GpuImage>(device_, image);
    const auto hash = hashImage(*image);
    entries_.emplace(hash, Entry{gpu, ++clock_});
    identities_[image.get()] = {image, gpu, hash};
    ++uploads_;
    uploadedBytes_ += image->pixels.size();
    trim();
    return gpu;
}
size_t GpuImageCache::missingBytes(const std::vector<std::shared_ptr<const ImageRGBA8>> &images) const {
    device_->checkThread();
    size_t bytes = 0;
    std::vector<std::shared_ptr<const ImageRGBA8>> missing;
    for (const auto &image : images) {
        if (!image)
            throw std::invalid_argument("Null GPU image source");
        validate(*image);
        auto identity = identities_.find(image.get());
        if (identity != identities_.end() && identity->second.source.lock() == image &&
            !identity->second.gpu.expired())
            continue;
        if (find(*image))
            continue;
        if (std::any_of(missing.begin(), missing.end(),
                        [&](const auto &other) { return equalImage(*image, *other); }))
            continue;
        missing.push_back(image);
        bytes += image->pixels.size();
    }
    return bytes;
}
void GpuImageCache::setIdleBudget(size_t bytes) {
    device_->checkThread();
    idleBudget_ = bytes;
    trim();
}
void GpuImageCache::trim() {
    device_->checkThread();
    size_t idle = 0;
    for (const auto &entry : entries_)
        if (entry.second.gpu.use_count() == 1)
            idle += entry.second.gpu->bytes();
    while (idle > idleBudget_) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.gpu.use_count() == 1 &&
                (oldest == entries_.end() || it->second.touched < oldest->second.touched))
                oldest = it;
        if (oldest == entries_.end())
            break;
        idle -= oldest->second.gpu->bytes();
        entries_.erase(oldest);
        ++evictions_;
    }
    // Keep identity metadata bounded, including aliases whose GPU image outlives its CPU pointer.
    for (auto it = identities_.begin(); it != identities_.end();) {
        if (it->second.source.expired() || it->second.gpu.expired())
            it = identities_.erase(it);
        else
            ++it;
    }
}
GpuImageCacheStats GpuImageCache::stats() const {
    device_->checkThread();
    GpuImageCacheStats result;
    result.entries = entries_.size();
    result.uploads = uploads_;
    result.uploadedBytes = uploadedBytes_;
    result.hits = hits_;
    result.evictions = evictions_;
    for (const auto &entry : entries_) {
        result.residentBytes += entry.second.gpu->bytes();
        if (entry.second.gpu.use_count() == 1)
            result.idleBytes += entry.second.gpu->bytes();
    }
    return result;
}
} // namespace render
