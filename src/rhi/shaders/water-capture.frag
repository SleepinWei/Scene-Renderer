#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 color;
layout(location=1) out vec4 positionValid;
layout(set=2,binding=5,std140) uniform WaterCapture {vec4 captureWaves;vec4 captureExtinction;mat4 coastInverse;mat4 coastModel;vec4 coastPatch;vec4 coastPreviousPatch;vec4 coastFeatures;vec4 coastBedInfo;vec4 captureEye;vec4 causticPatch;vec4 causticControls;vec4 causticMiddle;vec4 causticFar;vec4 causticProjection;};
layout(set=0,binding=7) uniform sampler2D captureShoreState;
layout(set=0,binding=6) uniform sampler2D captureCausticMap;
#include "water-caustics-sample.glsl"
layout(set=2,binding=6) uniform sampler2D captureDisplace;
layout(set=2,binding=7) uniform sampler2D captureDetailDisplace;
#include "water-waves.glsl"
#include "water-coast.glsl"
#define WATER_WET_ENABLED
#include "pbr-material.glsl"
#include "pbr-light-data.glsl"
#include "transparent-shadow.glsl"
#include "transparent-environment.glsl"
vec3 waterLightVisibility(int light,vec3 position,vec3 N,vec3 L) {
    float visibility=lightVisibility(light,position,N,L);
    if(lights[light].positionType.w>.5)return vec3(visibility);
    if(position.y>=coastHeight(position.xz,waterMacroHeight(position.xz,captureDisplace,captureWaves),captureShoreState,coastPatch,captureWaves.x,coastFeatures.z))return vec3(visibility);
    vec3 towardSun=-refract(-L,vec3(0,1,0),1./1.333);
    float distance=waterSunDistance(position,towardSun,captureDisplace,captureDetailDisplace,captureWaves,200.);
    if(coastFeatures.z>.5){float height=coastHeight(position.xz,waterMacroHeight(position.xz,captureDisplace,captureWaves),captureShoreState,coastPatch,captureWaves.x,1.);
        distance=max(height-position.y,0.)/max(towardSun.y,.05);}
    float f=.02037+.97963*pow(1.-max(L.y,0.),5.);
    return visibility*(1.-f)*exp(-captureExtinction.xyz*distance)*(light==int(causticControls.y)?waterCausticFactor(position,captureCausticMap,causticPatch,causticMiddle,causticFar,causticProjection,causticControls):vec3(1));
}
#undef RHI_VISIBILITY
#define RHI_VISIBILITY waterLightVisibility
#define RHI_TWO_SIDED (materialSettings.w>0.)
#include "pbr-lighting.glsl"
void main() {
    float height=waterHeight(worldPosition.xz,captureDisplace,captureDetailDisplace,captureWaves);
    if(coastFeatures.z>.5)height=coastDetailedHeight(worldPosition.xz,height,waterMacroHeight(worldPosition.xz,captureDisplace,captureWaves),captureShoreState,coastPatch,captureWaves.x,1.);
    // Keep independent nearest-depth layers in the two media, including when
    // the camera is underwater. Neither layer can erase the other at a pixel.
    if(captureEye.w>1.5) {
        // An independent air layer prevents submerged foreground geometry
        // from removing the surfaces needed by a bent transmission ray.
        if(worldPosition.y<height-.002)discard;
    } else if(worldPosition.y>=height-.002)discard;
    vec4 base=mappedBase();if(base.a<factors.w)discard;
    vec3 N=mappedNormal(),albedo=pow(max(base.rgb,vec3(0)),vec3(2.2));
    vec3 radiance=materialSettings.z>0?albedo+emissiveNormal.rgb:
        shadePbrExtended(worldPosition,N,mappedTangent(N),albedo,mappedMetallic(),mappedRoughness(),mappedAO(),emissiveNormal.rgb,mappedLobes(),0);
    color=vec4(max(radiance,vec3(0)),1);
    // Distinguish the terrain heightfield from independent mesh materials.
    positionValid=vec4(worldPosition,(materialFeatures.x&1)!=0?2.:1.);
}
