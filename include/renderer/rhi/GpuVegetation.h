#pragma once
#include "renderer/rhi/GpuGrass.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
namespace render {
class GpuTreeWind;
struct MeshPayload;
struct VegetationBatchStats {PlantType type;uint32_t lod,instances,triangles,fullDetailTriangles;uint32_t variant=0;};
struct VegetationCaptureBatch {DrawPacket packet;std::shared_ptr<const MeshPayload> geometry;MaterialDesc material;bool foliage=false;};
class GpuVegetation {
public:
    GpuVegetation(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,std::shared_ptr<GpuTerrain>,const glm::mat4&,const VegetationSettings&,std::shared_ptr<const ImageRGBA8> water={},std::shared_ptr<const ImageRGBA8> exclusion={});
    bool compatible(const VegetationSettings&)const;
    void update(const glm::mat4&,float,const VegetationSettings&);
    void append(std::vector<DrawPacket>&)const;
    std::vector<VegetationCaptureBatch> captureBatches()const;
    std::vector<VegetationBatchStats> readStats(); // Explicit diagnostic readback only.
private:
    struct Batch {VegetationSpecies species;uint32_t lod,variant,fullIndices;std::shared_ptr<GpuGrass> generator;std::shared_ptr<GpuMaterial> material,leafMaterial;std::shared_ptr<GpuMesh> leaves;std::shared_ptr<const MeshPayload> geometry,leafGeometry;MaterialDesc descriptor,leafDescriptor;std::shared_ptr<GpuTreeWind> barkWind,leafWind;TreeWindSettings wind;float seconds=0;};
    std::shared_ptr<rhi::GraphicsDevice> device_;
    std::vector<Batch> batches_;
};
void validateVegetation(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
