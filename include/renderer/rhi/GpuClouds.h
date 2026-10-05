#pragma once
#include "renderer/rhi/Resources.h"
#include "renderer/rhi/GpuAtmosphere.h"
#include "component/CloudSettings.h"
namespace render {
struct FrameData;
class GpuClouds {
public:
    GpuClouds(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    ~GpuClouds();
    void reset(){valid_=false;samples_=0;}
    void record(Resources&,rhi::CommandList&,const FrameData&,const SunState&,
                rhi::TextureViewHandle depth,rhi::TextureViewHandle skyIrradiance,
                rhi::TextureViewHandle hdr,rhi::TextureViewHandle motion);
    void commit(const FrameData&);
    std::vector<float> read();
    std::vector<float> readMetadata();
    std::array<uint32_t,3> readDispatch();
    uint32_t totalTiles() const;
    std::vector<uint8_t> readVoxels();
    std::vector<uint8_t> readDistance();
    std::vector<float> readLight();
private:
    struct Volume;std::unique_ptr<Volume> volume_;bool voxelBaked_=false;glm::vec3 previousSun_{0};
    struct Targets;std::unique_ptr<Targets> targets_;
    Resources resources_;rhi::SamplerHandle linear_,nearest_;
    rhi::TextureHandle noise_,weather_;rhi::TextureViewHandle noiseView_,weatherView_;
    struct Kernel {rhi::ComputePipelineHandle pipeline;std::vector<rhi::BindingLayout> layouts;};
    std::map<std::string,Kernel> kernels_;
    rhi::PipelineHandle composite_;std::vector<rhi::BindingLayout> compositeLayouts_;rhi::BufferHandle quad_;
    bool valid_=false,baked_=false;uint32_t seed_=0,index_=0,samples_=0;
    glm::mat4 previousVP_{1};glm::vec3 previousCamera_{0};float previousTime_=0;
    uint64_t previousKey_=0;CloudSettings previousSettings_;
};
void validateCloudsRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
