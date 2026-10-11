#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
#include <map>
#include <tuple>
namespace render {
struct RasterBatchStats {uint64_t sourcePackets=0,outputPackets=0,instances=0,poseUploadBytes=0,cacheHits=0;};
// Preview-only opaque ordering optimization. Transparent / generated / SSS
// packets are barriers; group opaque static geometry within each segment.
class RasterInstanceBatcher {
public:
    std::vector<DrawPacket> batch(const std::vector<DrawPacket>&);
    const RasterBatchStats& statistics()const{return stats_;}
private:
    using Key=std::tuple<const GpuMesh*,const GpuMaterial*,bool>;
    using CacheKey=std::pair<Key,uint64_t>;
    struct Record {std::shared_ptr<GpuMesh> base,batch;std::shared_ptr<GpuMaterial> material;std::vector<glm::mat4> poses;std::vector<uint64_t> ids;uint64_t id=0;};
    std::map<CacheKey,Record> records_;
    RasterBatchStats stats_;
};
void validateRasterInstanceBatching(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
