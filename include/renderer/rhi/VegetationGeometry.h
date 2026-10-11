#pragma once
#include "component/VegetationSpecies.h"
#include <memory>
namespace render {
struct MeshPayload;struct ImageRGBA8;
std::shared_ptr<const MeshPayload> plantGeometry(PlantType,uint32_t lod);
std::shared_ptr<const ImageRGBA8> plantPalette(PlantType);
}
