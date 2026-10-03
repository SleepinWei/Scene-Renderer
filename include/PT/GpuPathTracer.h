#pragma once
#include "PT/CpuPathTracer.h"
namespace pt {
// Compute-based software BVH tracing; device must belong to the calling thread.
Image renderGpu(const CpuScene &,const Options &,std::shared_ptr<rhi::GraphicsDevice>,
                const std::function<void(const Image &)> &progress = {});
void validateGpuPathTracing(std::shared_ptr<rhi::GraphicsDevice>);
} // namespace pt
