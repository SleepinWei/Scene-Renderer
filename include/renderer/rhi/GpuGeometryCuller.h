#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
namespace render {
struct CulledMeshDraw {rhi::BufferHandle indices,arguments;size_t argumentOffset=0;};
// Stable GPU index compaction. Original geometry remains available to every
// auxiliary view. Storage is reused on the owner's ordered GPU queue.
class GpuGeometryCuller {
public:
    GpuGeometryCuller(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    ~GpuGeometryCuller();
    std::vector<CulledMeshDraw> record(Resources&,rhi::CommandList&,const FrameData&,const std::vector<DrawPacket>&);
    void commit(); // Called only after successful submission; failed recordings cannot publish a cache entry.
    std::vector<TriangleCullCounts> readCounts(); // Explicit diagnostic readback only.
    uint64_t candidateTriangles() const {return candidateTriangles_;}
    size_t allocatedBytes() const {return allocatedBytes_;}
    size_t cacheHits() const {return cacheHits_;}
    size_t dispatches() const {return dispatches_;}
private:
    struct Storage;
    struct Kernel {rhi::ComputePipelineHandle pipeline;std::vector<rhi::BindingLayout> layouts;};
    Resources resources_;
    std::array<Kernel,4> kernels_;
    rhi::BufferHandle emptyArguments_;
    std::vector<std::unique_ptr<Storage>> slots_;
    uint64_t candidateTriangles_=0;
    size_t allocatedBytes_=0;
    size_t cacheHits_=0,dispatches_=0;
};
void validateGeometryCulling(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
