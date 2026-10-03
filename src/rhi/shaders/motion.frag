#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=2) in vec2 uv;layout(location=3) in vec4 previousClip;layout(location=4) in vec4 currentClip;layout(location=5) in float previousDepth;layout(location=6) flat in int reactive;
layout(location=0) out vec4 motion;
layout(set=1,binding=0,std140) uniform MaterialData {vec4 albedoAlpha;vec4 factors;vec4 emissiveNormal;};layout(set=1,binding=1) uniform sampler2D albedoMap;
#include "material-sampling.glsl"
void main(){if(sampleMaterial(albedoMap,uv).a*albedoAlpha.a<factors.w)discard;if(previousClip.w<=0 || currentClip.w<=0 || reactive!=0){motion=vec4(0,0,0,-1);return;}motion=vec4((previousClip.xy/previousClip.w-currentClip.xy/currentClip.w)*vec2(.5,-.5),previousDepth,1);}
