#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
enum class ShaderType {
    SIMPLE = 0,
    LIGHT,
    PBR,
    PBR_TESS,
    PBR_CLEARCOAT,
    PBR_ANISOTROPY,
    PBR_SSS,
    TERRAIN,
    SKYBOX,
    HDR,
    SKY,
    TEST,
    DEPTH,

    KIND_COUNT
};

struct RenderSetting {
    bool enableHDR = true;
    bool useDefer = true;
    bool enableShadow = true;
    bool enableRSM = false;
    bool enableDirectional = true;
    bool enableSSAO = true;
    bool enableTSAA = true;
    bool automaticQuality = false;
    float timeOverride = -1;
    float aoRadius = 1, aoBias = .025f, aoPower = 1.5f;
    bool aoHorizon=true,aoDenoise=true;
    int aoSlices=4,aoSteps=4;
    render::PostProcessSettings postProcess;
    render::RsmSettings rsmSettings;
    render::ShadowSettings shadowSettings;
};
