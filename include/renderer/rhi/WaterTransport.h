#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
namespace render {
struct WaterSlabEstimate {double multiple=0,single=0,bottom=0,standardError=0;};
// Unit extinction, black bottom, no boundary Fresnel. The existing surface
// shader supplies interface factors. Output is hemispherical flux, converted
// to isotropic outgoing radiance by the realtime approximation.
WaterSlabEstimate traceWaterSlab(float opticalThickness,float albedo,float g,float airCosine,uint32_t paths,uint32_t seed);
constexpr uint32_t WaterTauSamples=16,WaterAlbedoSamples=16,WaterGSamples=5,WaterAngleSamples=4;
const std::vector<glm::vec4>& waterTransportLut();
float waterMultipleEstimate(float opticalThickness,float albedo,float g,float airCosine);
void validateWaterTransport();
}
