#pragma once
#include "renderer/rhi/Resources.h"
#include "renderer/rhi/PostProcessSettings.h"
#include <glm/glm.hpp>
#include <memory>
namespace render {
struct PostProcessView {
    glm::mat4 view{1},viewProjection{1},unjitteredViewProjection{1}; // NDC depth 0..1, UV origin at top left.
    glm::vec3 cameraPosition{0};
    float time=0;
    uint64_t historyKey=0;
};
// Reusable native HDR -> display chain. UI is composited after this module.
// No scene ownership or CPU pixel readbacks; output is owned by the caller.
class GpuPostProcessor {
public:
    GpuPostProcessor(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory);
    ~GpuPostProcessor();
    void reset();
    void commit(); // Publish camera history only after successful submission.
    void record(Resources& frame,rhi::CommandList&,uint32_t width,uint32_t height,
                rhi::TextureViewHandle hdr,rhi::TextureViewHandle depth,rhi::TextureViewHandle output,
                const PostProcessSettings&,const PostProcessView&,float exposure=1,float gamma=2.2f,bool toneMapping=true);
private:
    struct Targets;
    Resources resources_;
    std::unique_ptr<Targets> bloom_,spatial_,display_;
    rhi::PipelineHandle down_,up_,spatialPipeline_,toneHdr_,toneOutput_,finish_;
    rhi::BindingLayout spatialLayout_,toneLayout_,finishLayout_,bloomLayout_;
    rhi::SamplerHandle sampler_,depthSampler_;
    rhi::BufferHandle quad_;
    glm::mat4 previousVP_{1},previousProjection_{1};
    glm::vec3 previousCamera_{0};
    uint64_t historyKey_=0;
    bool history_=false;
    uint32_t historyWidth_=0,historyHeight_=0;
    PostProcessView pendingView_;
    uint32_t pendingWidth_=0,pendingHeight_=0;
    bool pending_=false;
};
void validatePostProcessing(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory);
}
