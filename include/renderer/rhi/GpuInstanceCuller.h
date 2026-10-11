#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
namespace render {
struct CulledInstances {rhi::BufferHandle poses,arguments;};
// Light-view compaction consumes the original caster population, never a camera-visible list.
class GpuInstanceCuller {
public:
    GpuInstanceCuller(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    void reset();
    std::vector<CulledInstances> record(rhi::CommandList&,const FrameData&,const std::vector<DrawPacket>&,bool enabled);
    std::array<uint64_t,4> readStats(); // Diagnostic fence/readback only: input/output instances and triangles.
private:
    struct Slot {std::unique_ptr<Resources> resources;uint32_t capacity=0; rhi::BufferHandle poses,args;};
    struct Measure {rhi::BufferHandle input,output;uint32_t indices;};
    Resources resources_;
    std::unique_ptr<Resources> frameResources_;
    std::vector<rhi::BindingLayout> layouts_;
    rhi::ComputePipelineHandle prepare_,cull_;
    std::vector<Slot> slots_;
    std::vector<Measure> measures_;
    size_t cursor_=0;
};
}
