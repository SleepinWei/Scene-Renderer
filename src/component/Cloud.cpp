#include "component/Cloud.h"
#include <array>
#include <cmath>
#include <tuple>
bool render::CloudSettings::operator==(const CloudSettings& v) const {
    auto fields=[](const CloudSettings& s){return std::tie(s.enabled,s.temporal,s.baseHeight,s.thickness,s.coverage,s.density,
        s.shapeScale,s.weatherScale,s.erosion,s.maxDistance,s.wind.x,s.wind.y,s.steps,s.lightSteps,s.downsample,s.seed,s.voxel,s.distanceSkipping,s.coreIntegration,s.volumeCenter.x,s.volumeCenter.y,s.volumeCenter.z,
        s.volumeSize.x,s.volumeSize.y,s.volumeSize.z,s.voxelResolution,s.storm,s.lightning);};
    return fields(*this)==fields(v);
}
void render::CloudSettings::validate() const {
    for(float v:{baseHeight,thickness,coverage,density,shapeScale,weatherScale,erosion,maxDistance,wind.x,wind.y,volumeCenter.x,volumeCenter.y,volumeCenter.z,volumeSize.x,volumeSize.y,volumeSize.z,storm,lightning})
        if(!std::isfinite(v))throw std::invalid_argument("Cloud nonfinite setting");
    if(baseHeight<100 || baseHeight>15000 || thickness<100 || thickness>10000 || coverage<0 || coverage>1 ||
       density<0 || density>.02f || shapeScale<100 || weatherScale<1000 || erosion<0 || erosion>1 ||
       maxDistance<1000 || maxDistance>200000 || steps<16 || steps>192 || lightSteps<1 || lightSteps>12 ||
       (downsample!=1 && downsample!=2 && downsample!=4) || glm::length(wind)>200 ||
       glm::any(glm::lessThan(volumeSize,glm::vec3(100))) || glm::any(glm::greaterThan(volumeSize,glm::vec3(50000))) ||
       (voxelResolution!=64 && voxelResolution!=128) || storm<0 || storm>1 || lightning<0 || lightning>100)
        throw std::invalid_argument("Cloud setting outside supported range");
}
void Cloud::loadFromJson(json& j) {
    auto v=settings();
    v.coreIntegration=j.value("coreIntegration",v.coreIntegration);v.voxel=j.value("voxel",v.voxel);v.distanceSkipping=j.value("distanceSkipping",v.distanceSkipping);
    v.voxelResolution=j.value("voxelResolution",v.voxelResolution);
    v.storm=j.value("storm",v.storm);v.lightning=j.value("lightning",v.lightning);
    if(j.contains("volumeCenter")){auto a=j.at("volumeCenter").get<std::array<float,3>>();v.volumeCenter={a[0],a[1],a[2]};}
    if(j.contains("volumeSize")){auto a=j.at("volumeSize").get<std::array<float,3>>();v.volumeSize={a[0],a[1],a[2]};}
    v.enabled=j.value("enabled",v.enabled);v.temporal=j.value("temporal",v.temporal);
    v.baseHeight=j.value("baseHeight",v.baseHeight);v.thickness=j.value("thickness",v.thickness);
    v.coverage=j.value("coverage",v.coverage);v.density=j.value("density",v.density);
    v.shapeScale=j.value("shapeScale",v.shapeScale);v.weatherScale=j.value("weatherScale",v.weatherScale);
    v.erosion=j.value("erosion",v.erosion);v.maxDistance=j.value("maxDistance",v.maxDistance);
    v.steps=j.value("steps",v.steps);v.lightSteps=j.value("lightSteps",v.lightSteps);
    v.downsample=j.value("downsample",v.downsample);v.seed=j.value("seed",v.seed);
    if(j.contains("wind")){auto w=j.at("wind").get<std::array<float,2>>();v.wind={w[0],w[1]};}
    setSettings(v);
}
