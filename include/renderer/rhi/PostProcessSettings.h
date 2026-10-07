#pragma once
namespace render {
enum class ToneMapper { Exponential, ACES, Reinhard, Linear };
// Camera/display effects; defaults preserve the existing exponential HDR output.
struct PostProcessSettings {
    bool enabled=true;
    ToneMapper toneMapper=ToneMapper::Exponential;
    float exposureEV=0;
    bool bloom=false;
    float bloomThreshold=1,bloomKnee=.5f,bloomStrength=.15f,bloomScatter=.7f;
    bool depthOfField=false;
    float focusDistance=10,focusRange=5,dofRadius=8;
    bool motionBlur=false; // Camera motion reconstructed from opaque depth.
    float shutter=.5f,motionMaxPixels=24;
    bool colorGrading=false;
    float saturation=1,contrast=1,temperature=0,tint=0;
    bool fxaa=false,sharpen=false;
    float sharpness=.25f;
    bool vignette=false;
    float vignetteStrength=.3f,vignetteRoundness=.6f;
    bool chromaticAberration=false;
    float chromaticPixels=1;
    bool filmGrain=false;
    float grainStrength=.025f;
};
void validatePostProcessSettings(const PostProcessSettings&);
}
