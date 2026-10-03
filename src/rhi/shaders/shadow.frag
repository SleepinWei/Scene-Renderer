#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 flux;
layout(location=1) out vec4 position;
layout(location=2) out vec4 normal;
layout(set=0,binding=2,std140) uniform RsmLight {vec4 lightPositionType;vec4 lightColorInner;vec4 lightDirectionOuter;vec4 captureSettings;};
layout(set=0,binding=4) uniform sampler2D skyIrradiance;
#include "sky-mapping.glsl"
#include "pbr-material.glsl"

void main(){
    vec4 base=mappedBase();if(base.a<factors.w)discard;
    vec3 N=mappedNormal(), delta=lightPositionType.w<.5?-lightDirectionOuter.xyz:lightPositionType.xyz-worldPosition;
    vec3 L=length(delta)>1e-6?normalize(delta):N;
    float cone=lightPositionType.w>1.5?clamp((dot(L,-normalize(lightDirectionOuter.xyz))-lightDirectionOuter.w)/max(lightColorInner.w-lightDirectionOuter.w,1e-5),0,1):1;
    vec3 incident=lightColorInner.rgb*max(dot(N,L),0)*cone;
    if(captureSettings.y>0. && lightPositionType.w>.5)incident/=max(dot(delta,delta),1e-6);
    if(captureSettings.x>0.)incident+=3.14159265*sampleSkyLut(skyIrradiance,N);
    float area=captureSettings.y>0.?length(cross(dFdx(worldPosition),dFdy(worldPosition))):1.;
    float metallic=mappedMetallic();
    flux=vec4(pow(max(base.rgb,vec3(0)),vec3(2.2))*incident*(1.-metallic)*area,1);
    position=vec4(worldPosition,1);normal=vec4(N,1);
}
