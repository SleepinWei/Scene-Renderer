#pragma once
#include "renderer/rhi/GpuGeometryCuller.h"
namespace render {
// Selected-occluder prepass or external G-buffer max-depth pyramid.
// The main-camera results never
// replace the original geometry in shadows, reflection or refraction captures.
class GpuHiZCuller {
public:
    GpuHiZCuller(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    ~GpuHiZCuller();
    // External-depth mode allocates no depth prepass attachment; only the chain.
    bool prepareDepth(uint32_t width,uint32_t height,uint32_t baseMip=0);
    void recordDepthPyramid(Resources&,rhi::CommandList&,rhi::TextureViewHandle,const char* prefix);
    void recordDepthCopy(Resources&,rhi::CommandList&,rhi::TextureViewHandle source,rhi::TextureViewHandle target,uint32_t width,uint32_t height);
    void fusedBuild(bool enabled) {fusedBuild_=enabled;}
    bool prepare(const FrameData&,const std::vector<DrawPacket>&);
    std::vector<CulledMeshDraw> record(Resources&,rhi::CommandList&,const FrameData&,
        const std::vector<DrawPacket>&,const std::vector<CulledMeshDraw>&);
    void recordPyramid(Resources&,rhi::CommandList&,const FrameData&,const std::vector<DrawPacket>&,const std::vector<CulledMeshDraw>&);
    std::vector<CulledMeshDraw> recordVisibility(Resources&,rhi::CommandList&,const FrameData&,const std::vector<DrawPacket>&,const std::vector<CulledMeshDraw>&);
    rhi::TextureViewHandle pyramidView() const;
    uint32_t pyramidLevels() const;
    uint32_t pyramidBaseMip() const;
    HiZCullStats stats() const {return stats_;}
    void cancel() {stats_={};committed_=false;}
    void commit() {committed_=true;} // Only after successful main submission.
    std::vector<HiZCullCount> readCounts();
    std::vector<float> readDepth(uint32_t mip);
    std::vector<HiZCullBounds> bounds() const;
private:
    struct Targets;
    struct alignas(16) Input {glm::vec4 rectangle,depth;glm::uvec4 counts;};
    static_assert(sizeof(Input)==48,"HiZ bounds std430 ABI");
    Resources resources_;
    std::unique_ptr<Targets> targets_;
    rhi::PipelineHandle depthPipeline_;
    rhi::ComputePipelineHandle buildPipeline_,fusedPipeline_,testPipeline_,combinePipeline_;
    rhi::SamplerHandle sampler_;
    std::vector<Input> inputs_;
    std::vector<size_t> occluders_;
    HiZCullStats stats_;
    bool committed_=false;
    bool fusedBuild_=false;
};
void validateHiZCulling(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
