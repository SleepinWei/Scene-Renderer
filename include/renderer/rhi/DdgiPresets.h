#pragma once
#include "renderer/rhi/DdgiSettings.h"
#include <string>
namespace render {
inline DdgiSettings ddgiScenePreset(const std::string& scene) {
    DdgiSettings gi;gi.enabled=true;
    if(scene=="cornell"){gi.origin={-3.5f,-.5f,-3.5f};gi.counts={8,8,8};gi.spacing=1;gi.maxDistance=20;gi.probesPerFrame=16;gi.normalBias=.08f;gi.viewBias=.02f;}
    // Sponza is normalized to 12 m tall, with bounds about +/-14.35 m in X and +/-8.82 m in Z.
    if(scene=="sponza"){gi.origin={-15,-1,-9};gi.counts={16,8,10};gi.spacing=2;gi.maxDistance=45;gi.probesPerFrame=8;gi.normalBias=.08f;gi.viewBias=.02f;}
    return gi;
}
}
