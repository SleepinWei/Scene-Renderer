#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
namespace render {
struct WeatherLightingSettings {
    bool enabled=false,cloudShadows=true,fog=true,aerialPerspective=true,aerialShadows=true;
    float dryExtinction=.000015f,rainExtinction=.00005f,height=180,shadowInterval=.1f;
    float cloudShadowStrength=.3f;
    float aerialDistanceScale=2,aerialStartDistance=100,aerialMaxDistance=32000;
    glm::vec3 fogColour{.55f,.65f,.75f};
    void validate() const;
};
// Frozen 2.5D solar transmission, not a spatial scattering medium.
struct CloudShadowField {
    uint32_t width=128;
    glm::vec4 patch{0}; // world XZ minimum, extent
    glm::vec4 projection{0}; // sun X/Y,Z/Y, reference plane, maximum receiver height (0 disables)
    std::vector<float> transmission;
    float sample(glm::vec3 position) const;
    void validate() const;
};
std::string serializeCloudShadow(const CloudShadowField&);
std::shared_ptr<const CloudShadowField> deserializeCloudShadow(const std::string&);
float airOpticalDepth(glm::vec3 origin,glm::vec3 endpoint,float baseHeight,float height,float extinction,float waterLevel=-1e30f);
}
