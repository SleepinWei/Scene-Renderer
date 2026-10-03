#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=1,binding=1) uniform sampler2D positionBuffer;
layout(set=1,binding=2) uniform sampler2D normalBuffer;
layout(set=1,binding=3) uniform sampler2D albedoBuffer;
layout(set=1,binding=4) uniform sampler2D emissiveBuffer;
#include "pbr-light-data.glsl"
#include "shadow-sampling.glsl"
#include "environment.glsl"
#include "sky-display.glsl"
bool twoSided=false;
#define RHI_TWO_SIDED twoSided
#include "pbr-lighting.glsl"
layout(set=1,binding=6) uniform sampler2D aoBuffer;
layout(set=2,binding=0) uniform sampler2D materialEffectsBuffer;layout(set=2,binding=1) uniform sampler2D tangentBuffer;layout(set=2,binding=2) uniform sampler2D backDepthBuffer;
void main() {
    vec4 position = texture(positionBuffer,uv);
    if(position.w==0){vec4 p=inverseViewProjection*vec4(uv*vec2(2,-2)+vec2(-1,1),1,1);vec3 direction=normalize(p.xyz/p.w-skyCamera.xyz);color=vec4(skySettings.x==0?vec3(0):displayedSky(direction),1);return;}
    twoSided=position.w>1.5;
    vec4 normal = texture(normalBuffer,uv), albedo = texture(albedoBuffer,uv), emissive = texture(emissiveBuffer,uv);
    if(texture(materialEffectsBuffer,uv).w<0){color=vec4(emissive.xyz,1);return;}
    vec3 direct=shadePbrExtended(position.xyz,normalize(normal.xyz),normalize(texture(tangentBuffer,uv).xyz),albedo.xyz,albedo.w,normal.w,emissive.w*texture(aoBuffer,uv).r,emissive.xyz,texture(materialEffectsBuffer,uv),max(texture(backDepthBuffer,uv).r-texture(tangentBuffer,uv).w,0.));
    vec3 indirect=indirectRsm(position.xyz,normalize(normal.xyz),albedo.xyz,normal.w);
    color=vec4(rsmSettings.w>0. || skySettings.y>0.?indirect:direct+indirect,1);
}
