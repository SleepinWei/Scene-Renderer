#pragma once
#include <atomic>
#include <cstdint>
namespace engine {
inline uint64_t nextIdentity() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}
// Mutations and revision bumps are owned by the logic thread. A live asset copy
// is a distinct object. Snapshot payloads carry the original ID explicitly.
struct AssetIdentity {
    AssetIdentity() = default;
    AssetIdentity(const AssetIdentity &other) : contentRevision_(other.contentRevision_) {}
    const uint64_t assetId = nextIdentity();
    uint64_t getContentRevision() const { return contentRevision_; }

  protected:
    void invalidate() { ++contentRevision_; }

  private:
    uint64_t contentRevision_ = 1;
};
} // namespace engine
