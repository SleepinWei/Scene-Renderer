#pragma once
#include "renderer/rhi/SceneSnapshot.h"
namespace render {
class GpuTreeWind {
public:
    GpuTreeWind(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,std::shared_ptr<GpuMesh>,std::shared_ptr<const MeshPayload>);
    void record(rhi::CommandList&,float,const TreeWindSettings&);
private:
    std::shared_ptr<GpuMesh> mesh_;
    Resources resources_;
    rhi::BufferHandle rest_,data_;
    rhi::ComputePipelineHandle pipeline_;
    rhi::BindingLayout layout_;
    std::unique_ptr<Resources> frame_;
    bool history_=false;
    float seconds_=0;
    TreeWindSettings settings_;
};
std::shared_ptr<const MeshPayload> freezeTreeWind(const MeshPayload&,float,const TreeWindSettings&);
}
