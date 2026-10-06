#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 positionValid;
layout(location=1) out vec4 normalRoughness;
layout(location=2) out vec4 albedoMetallic;
layout(location=3) out vec4 emissiveAO;
#define WATER_WET_ENABLED
#include "pbr-material.glsl"
void main() {
    vec4 base = mappedBase();
    if (base.a < factors.w) discard;
    positionValid = vec4(worldPosition,1);
    normalRoughness = vec4(mappedNormal(),mappedRoughness());
    albedoMetallic = vec4(pow(max(base.rgb,vec3(0)),vec3(2.2)),mappedMetallic());
    emissiveAO = vec4(emissiveNormal.rgb,mappedAO());
}
