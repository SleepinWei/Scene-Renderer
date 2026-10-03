#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=2,binding=5) uniform sampler2D indirectBuffer;
layout(set=2,binding=6) uniform sampler2D aoBuffer;
layout(set=2,binding=7) uniform sampler2D backDepthBuffer;
#include "pbr-material.glsl"
#include "pbr-light-data.glsl"
#include "transparent-shadow.glsl"
#include "transparent-environment.glsl"
#define RHI_TWO_SIDED (materialSettings.w>0.)
#include "pbr-lighting.glsl"
void main() {
    vec2 screenUV=gl_FragCoord.xy/vec2(textureSize(aoBuffer,0));
    vec4 base = texture(albedoMap, uv) * albedoAlpha;
    if (base.a < factors.w) discard;
    // Preserve the existing PBR path's manual albedo decoding and HDR curve.
    vec3 albedo = pow(max(base.rgb,vec3(0)), vec3(2.2));
    float metallic = clamp(texture(metallicMap, uv).b * factors.x,0,1);
    float roughness = clamp(texture(roughnessMap, uv).g * factors.y,0.045,1);
    float ao = mix(1.0, texture(aoMap, uv).r, factors.z)*texture(aoBuffer,screenUV).r;
    vec3 N = mappedNormal();
    if(materialSettings.z>0){color=vec4(albedo+emissiveNormal.rgb,base.a);return;}
    color = vec4(shadePbrExtended(worldPosition,N,mappedTangent(N),albedo,metallic,roughness,ao,emissiveNormal.rgb,mappedLobes(),max(texture(backDepthBuffer,screenUV).r-gl_FragCoord.z,0.))+texture(indirectBuffer,screenUV).rgb,1);
}
