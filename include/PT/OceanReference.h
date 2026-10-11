#pragma once
#include "PT/CpuPathTracer.h"
#include "rhi/GraphicsDevice.h"
namespace pt {
// Frozen-time raster capture from the same unfrozen snapshot used by PT.
void captureOceanReference(const render::RenderWorldSnapshot&,const CpuScene&,const Options&,
                           std::shared_ptr<rhi::GraphicsDevice>,const std::string& prefix);
}
