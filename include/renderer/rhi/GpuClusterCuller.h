#pragma once
#include "renderer/rhi/GpuGeometryCuller.h"
#include "renderer/rhi/ClusterGeometry.h"
namespace render {
class GpuClusterCuller {
public:
    GpuClusterCuller(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    ~GpuClusterCuller();
    std::vector<CulledMeshDraw> record(Resources&,rhi::CommandList&,const FrameData&,const std::vector<DrawPacket>&,std::vector<CulledMeshDraw>,bool twoPhase=false,rhi::TextureViewHandle history={},uint32_t levels=0,const glm::mat4& historyVP=glm::mat4(1));
    std::vector<CulledMeshDraw> recordOcclusion(Resources&,rhi::CommandList&,const FrameData&,const std::vector<DrawPacket>&,std::vector<CulledMeshDraw>,rhi::TextureViewHandle,uint32_t);
    std::vector<CulledMeshDraw> recordLate(Resources&,rhi::CommandList&,const FrameData&,const std::vector<DrawPacket>&,rhi::TextureViewHandle,uint32_t);
    std::vector<ClusterVisibility> readVisibility();
    void commit();
    ClusterCullStats stats()const{return stats_;}
    std::vector<ClusterCullCount> readCounts();
    std::vector<uint32_t> readIndices(size_t packet); // diagnostics only.
private:
    struct Batch;
    std::shared_ptr<Batch> batch_;
    std::string directory_;
    bool recordBatched(Resources&,rhi::CommandList&,const FrameData&,const std::vector<DrawPacket>&,std::vector<CulledMeshDraw>&,rhi::TextureViewHandle={},uint32_t=0,uint32_t mode=0,const glm::mat4& historyVP=glm::mat4(1));
    std::vector<ClusterCullCount> readBatchCounts();
    std::vector<uint32_t> readBatchIndices(size_t);
    struct Slot;
    Resources resources_;
    struct Kernel {rhi::ComputePipelineHandle pipeline;std::vector<rhi::BindingLayout> layouts;};
    std::array<Kernel,3> kernels_;
    std::vector<std::unique_ptr<Slot>> slots_;
    ClusterCullStats stats_;
    bool committed_=false;
};
}
