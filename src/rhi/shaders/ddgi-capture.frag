#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 worldPosition;layout(location=1) in vec3 worldNormal;layout(location=2) in vec2 uv;
layout(location=0) out vec4 sampleRadianceDistance;
#include "pbr-material.glsl"
#include "pbr-light-data.glsl"
#include "transparent-shadow.glsl"
#define DDGI_SET 2
#define DDGI_BINDING 0
#include "ddgi-parameters.glsl"
layout(set=2,binding=5,std430) readonly buffer DdgiField {vec4 ddgiField[];};
#include "ddgi-query.glsl"
void main(){
    vec4 base=mappedBase();if(base.a<factors.w)discard;
    vec3 albedo=pow(max(base.rgb,vec3(0)),vec3(2.2)),N=mappedNormal();
    vec3 ray=worldPosition-ddgiProbe.xyz;float distance=length(ray);if(distance>ddgiCapture.y)discard;
    bool back=materialSettings.w<.5 && dot(normalize(worldNormal),ray)>0.;
    if(materialSettings.w>.5 && dot(N,ray)>0.)N=-N;
    vec3 outgoing=emissiveNormal.rgb;
    if(materialSettings.z>0.)outgoing+=albedo;
    else if(!back){
        vec3 incident=vec3(0);
        for(int i=0;i<counts.x;i++){
            vec3 delta=lights[i].positionType.xyz-worldPosition;float d=length(delta);
            vec3 L=lights[i].positionType.w==0.?-normalize(lights[i].directionOuter.xyz):delta/max(d,1e-5);
            float attenuation=lights[i].positionType.w>.5 && counts.y>0?1./max(d*d,1.):1.;
            if(lights[i].positionType.w==2.)attenuation*=clamp((dot(L,-normalize(lights[i].directionOuter.xyz))-lights[i].directionOuter.w)/max(lights[i].colorInner.w-lights[i].directionOuter.w,1e-5),0.,1.);
            incident+=lights[i].colorInner.rgb*max(dot(N,L),0.)*attenuation*lightVisibility(i,worldPosition,N,L)/3.14159265;
        }
        if(ddgiTemporal.w>0.)incident+=ddgiQuery(worldPosition,N,-ray/max(distance,1e-5)).rgb*ddgiTemporal.w;
        outgoing+=albedo*(1.-mappedMetallic())*incident;
    }
    sampleRadianceDistance=vec4(clamp(outgoing,vec3(0),vec3(10000)),back?-distance:distance);
}
