#pragma once
#include "renderer/rhi/SceneSnapshot.h"
#include "component/VegetationSpecies.h"
namespace render {
struct TreeGeometry {
    std::shared_ptr<const MeshPayload> bark,foliage;
    uint32_t branches=0,leaves=0;
};
TreeGeometry treeGeometry(PlantType,uint32_t lod,uint32_t seed);
MaterialDesc treeMaterial(PlantType,bool foliage,uint32_t lod=0);
}
