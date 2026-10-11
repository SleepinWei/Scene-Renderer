#pragma once
#include "renderer/rhi/Resources.h"
namespace render {
struct FrameData;
// Analytic, camera-local visual rain. The CPU environment owns precipitation.
class GpuRain {
public:
    GpuRain(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    void record(Resources&,rhi::CommandList&,const FrameData&,rhi::TextureViewHandle hdr,rhi::TextureViewHandle depth);
private:
    Resources resources_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle vertices_;
    rhi::SamplerHandle sampler_;
    rhi::BindingLayout layout_;
};
void validateRain(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
