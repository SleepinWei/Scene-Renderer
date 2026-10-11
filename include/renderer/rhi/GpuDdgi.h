#pragma once
#include "renderer/rhi/Resources.h"
#include "renderer/rhi/DdgiSettings.h"
namespace pt {class GpuProbeTracer;}
namespace render {
struct FrameData;struct DrawPacket;
class GpuDdgi {
public:
    GpuDdgi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    ~GpuDdgi();
    void update(Resources&,rhi::CommandList&,const FrameData&,const std::vector<DrawPacket>&,rhi::BufferHandle lighting,rhi::BufferHandle shadows,rhi::TextureViewHandle shadowAtlas,rhi::TextureViewHandle sky);
    void compose(Resources&,rhi::CommandList&,const FrameData&,rhi::TextureViewHandle hdr,const std::vector<rhi::TextureViewHandle>& gbuffer,rhi::TextureViewHandle ao,rhi::BufferHandle skyParameters,rhi::TextureViewHandle skyIrradiance);
    void visualize(Resources&,rhi::CommandList&,const FrameData&,rhi::TextureViewHandle output,rhi::TextureViewHandle positions,float exposure,float gamma);
    const DdgiStats& stats() const {return stats_;}
    void setProbeTracer(std::shared_ptr<pt::GpuProbeTracer> tracer){probeTracer_=std::move(tracer);}
    // Explicit diagnostic readback, never used by the interactive frame loop.
    std::vector<glm::vec4> sample(const std::vector<glm::vec4>& positions,const std::vector<glm::vec4>& normals);
    std::vector<glm::vec4> readField() const;
    std::vector<float> readCapture() const; // Raw last-updated probe: RGB outgoing radiance, signed radial distance.
private:
    struct Volume;std::unique_ptr<Volume> volume_;
    Resources resources_;std::string directory_;
    std::vector<rhi::BindingLayout> captureLayouts_;
    rhi::BindingLayout integrateLayout_,copyLayout_,surfaceLayout_,environmentLayout_,sampleLayout_;
    rhi::PipelineHandle capture_,instanced_,surface_;
    rhi::ComputePipelineHandle integrate_,copy_,sample_;
    rhi::SamplerHandle nearest_,linear_;rhi::BufferHandle quad_;
    rhi::BindingLayout debugVertexLayout_,debugFragmentLayout_;
    rhi::PipelineHandle debugPipeline_;
    DdgiStats stats_;
    std::shared_ptr<pt::GpuProbeTracer> probeTracer_;
    rhi::BufferHandle emptyProbe_;
};
void validateDdgi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
