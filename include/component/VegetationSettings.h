#pragma once
#include <cmath>
#include <stdexcept>
#include <string>
#include <cstdint>
#include "component/VegetationSpecies.h"
#include "renderer/rhi/TreeWind.h"

// World-space placement controls copied into immutable render snapshots.
struct VegetationSettings {
    uint32_t capacity = 65536, maxLod = 1, samplesPerCell = 2;
    float density = 1, distance = 100, fadeStart = 70;
    float minimumNormalY = .65f, waterLevel = -100000, shoreMargin = .25f;
    float heightScale = 1, widthScale = 1, maximumAltitude = 100000;
    // Zero nearSpacing retains the legacy fixed per-cell distribution.
    float nearSpacing=0,farSpacing=3.5f,denseRadius=10,sparseRadius=70;
    float exclusionSeaLevel=0,exclusionHeightRange=0,exclusionSlopeMin=.7f,exclusionSlopeMax=.95f;
    bool frustumCull = true;
    // Frozen runtime wind input; false preserves the legacy animation.
    bool environmentWind=false;
    float windX=0,windZ=0,windPhase=0;
    std::string waterMaskPath;
    std::vector<VegetationSpecies> species; // Empty keeps the original blade batch.
    bool speciesLod=true;
    uint32_t placementSeed=0;
    int geometryLod=-1; // -1 original, 0 near, 1 far (complementary selection).
    float lodDistance=25,lodTransition=8,geometryRadius=.4f;
    float farWidthScale=1;
    bool stableLattice=false;
    uint32_t geometryVariant=0,geometryVariants=1;
    render::TreeWindSettings treeWind;
    void validate() const {
        treeWind.validate();
        size_t total=0;for(const auto& s:species){s.validate();total+=size_t(s.capacity)*s.variants;}
        if(!geometryVariants||geometryVariants>8||geometryVariant>=geometryVariants||species.size()>16||total>262144||placementSeed>16777215||geometryLod<-1||geometryLod>1)throw std::invalid_argument("Vegetation species budget exceeded");
        for(float x:{lodDistance,lodTransition,geometryRadius,farWidthScale})if(!std::isfinite(x))throw std::invalid_argument("Nonfinite vegetation geometry");
        if(lodDistance<=0||lodTransition<0||lodTransition>lodDistance||geometryRadius<=0||farWidthScale<1||farWidthScale>4)throw std::invalid_argument("Invalid vegetation geometry");
        for (float v : {density,distance,fadeStart,minimumNormalY,waterLevel,shoreMargin,
                        heightScale,widthScale,maximumAltitude,nearSpacing,farSpacing,denseRadius,sparseRadius,
                        exclusionSeaLevel,exclusionHeightRange,exclusionSlopeMin,exclusionSlopeMax,windX,windZ,windPhase})
            if (!std::isfinite(v)) throw std::invalid_argument("Nonfinite vegetation setting");
        if (!capacity || capacity > 1048576 || maxLod > 5 || (samplesPerCell != 2 && samplesPerCell != 4 && samplesPerCell != 8) || density < 0 || density > 1 ||
            nearSpacing<0 || (nearSpacing>0 && (farSpacing<nearSpacing || denseRadius<0 || sparseRadius<=denseRadius)) ||
            exclusionHeightRange<0 || exclusionSlopeMin<0 || exclusionSlopeMax>1 || exclusionSlopeMin>=exclusionSlopeMax ||
            distance <= 0 || fadeStart < 0 || fadeStart >= distance || minimumNormalY < 0 ||
            minimumNormalY > 1 || shoreMargin < 0 || heightScale <= 0 || widthScale <= 0)
            throw std::invalid_argument("Invalid vegetation placement settings");
    }
};
