#pragma once
#include <cmath>
#include <stdexcept>
#include <string>
#include <cstdint>

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
    std::string waterMaskPath;
    void validate() const {
        for (float v : {density,distance,fadeStart,minimumNormalY,waterLevel,shoreMargin,
                        heightScale,widthScale,maximumAltitude,nearSpacing,farSpacing,denseRadius,sparseRadius,
                        exclusionSeaLevel,exclusionHeightRange,exclusionSlopeMin,exclusionSlopeMax})
            if (!std::isfinite(v)) throw std::invalid_argument("Nonfinite vegetation setting");
        if (!capacity || capacity > 1048576 || maxLod > 5 || (samplesPerCell != 2 && samplesPerCell != 4 && samplesPerCell != 8) || density < 0 || density > 1 ||
            nearSpacing<0 || (nearSpacing>0 && (farSpacing<nearSpacing || denseRadius<0 || sparseRadius<=denseRadius)) ||
            exclusionHeightRange<0 || exclusionSlopeMin<0 || exclusionSlopeMax>1 || exclusionSlopeMin>=exclusionSlopeMax ||
            distance <= 0 || fadeStart < 0 || fadeStart >= distance || minimumNormalY < 0 ||
            minimumNormalY > 1 || shoreMargin < 0 || heightScale <= 0 || widthScale <= 0)
            throw std::invalid_argument("Invalid vegetation placement settings");
    }
};
