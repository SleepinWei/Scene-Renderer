#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 receiverPosition;
layout(location=1) out vec4 receiverNormal;
layout(set=0,binding=2,std140) uniform CausticReceiverSettings {vec4 receiverSettings;};
layout(set=1,binding=0,std140) uniform MaterialData {vec4 albedoAlpha;vec4 factors;vec4 emissiveNormal;};
layout(set=1,binding=1) uniform sampler2D albedoMap;
#include "material-sampling.glsl"
void main(){
    if(worldPosition.y>=receiverSettings.z-.025)discard;
    if(sampleMaterial(albedoMap,uv).a*albedoAlpha.a<factors.w)discard;
    receiverPosition=vec4(worldPosition,1);receiverNormal=vec4(normalize(worldNormal),1);
}
