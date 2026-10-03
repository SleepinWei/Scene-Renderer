#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 positionValid;
layout(location=1) out vec4 normalRoughness;
layout(location=2) out vec4 albedoMetallic;
layout(location=3) out vec4 emissiveAO;
#include "pbr-material.glsl"
void main() {
    vec4 base = sampleMaterial(albedoMap, uv) * albedoAlpha;
    if (base.a < factors.w) discard;
    positionValid = vec4(worldPosition,1);
    normalRoughness = vec4(mappedNormal(),clamp(sampleMaterial(roughnessMap,uv).g*factors.y,.045,1));
    albedoMetallic = vec4(pow(max(base.rgb,vec3(0)),vec3(2.2)),clamp(sampleMaterial(metallicMap,uv).b*factors.x,0,1));
    emissiveAO = vec4(emissiveNormal.rgb,mix(1.0,sampleMaterial(aoMap,uv).r,factors.z));
}
