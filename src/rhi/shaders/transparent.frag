#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 color;
layout(location=1) out vec4 motion;
#include "pbr-material.glsl"
#include "pbr-light-data.glsl"
#include "transparent-shadow.glsl"
#include "transparent-environment.glsl"
#define RHI_TWO_SIDED (materialSettings.w>0.)
#include "pbr-lighting.glsl"
void main() {
    motion=vec4(0,0,0,-1);
    vec4 base = sampleMaterial(albedoMap, uv) * albedoAlpha;
    if (base.a < factors.w) discard;
    // Preserve the existing PBR path's manual albedo decoding and HDR curve.
    vec3 albedo = pow(max(base.rgb,vec3(0)), vec3(2.2));
    float metallic = clamp(sampleMaterial(metallicMap, uv).b * factors.x,0,1);
    float roughness = clamp(sampleMaterial(roughnessMap, uv).g * factors.y,0.045,1);
    float ao = mix(1.0, sampleMaterial(aoMap, uv).r, factors.z);
    vec3 N = mappedNormal();
    if(materialSettings.z>0){color=vec4(albedo+emissiveNormal.rgb,base.a);return;}
    color = vec4(shadePbrExtended(worldPosition,N,mappedTangent(N),albedo,metallic,roughness,ao,emissiveNormal.rgb,mappedLobes(),texture(specialMap,uv).a*materialSettings.x),base.a);
}
