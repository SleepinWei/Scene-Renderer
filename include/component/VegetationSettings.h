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
    bool frustumCull = true;
    std::string waterMaskPath;
    void validate() const {
        for (float v : {density,distance,fadeStart,minimumNormalY,waterLevel,shoreMargin,
                        heightScale,widthScale,maximumAltitude})
            if (!std::isfinite(v)) throw std::invalid_argument("Nonfinite vegetation setting");
        if (!capacity || capacity > 1048576 || maxLod > 5 || (samplesPerCell != 2 && samplesPerCell != 4 && samplesPerCell != 8) || density < 0 || density > 1 ||
            distance <= 0 || fadeStart < 0 || fadeStart >= distance || minimumNormalY < 0 ||
            minimumNormalY > 1 || shoreMargin < 0 || heightScale <= 0 || widthScale <= 0)
            throw std::invalid_argument("Invalid vegetation placement settings");
    }
};
