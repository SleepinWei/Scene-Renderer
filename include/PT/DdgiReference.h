#pragma once
#include "PT/CpuPathTracer.h"
#include "rhi/GraphicsDevice.h"
namespace pt {
// Explicit offline capture from the exact PT snapshot; never part of the editor loop.
void captureDdgiReference(const render::RenderWorldSnapshot&,const CpuScene&,const Options&,
                          std::shared_ptr<rhi::GraphicsDevice>,const std::string& prefix,
                          const std::string& scene,uint32_t frames,bool pathCapture=false,uint32_t pathSamples=4,uint32_t pathDepth=8,
                          const CpuScene *probeWorld=nullptr);
}
