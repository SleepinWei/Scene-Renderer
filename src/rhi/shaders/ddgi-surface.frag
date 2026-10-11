#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;layout(location=0) out vec4 colour;
#include "ddgi-parameters.glsl"
layout(set=0,binding=1,std430) readonly buffer DdgiField {vec4 ddgiField[];};
layout(set=0,binding=2) uniform sampler2D positions;
layout(set=0,binding=3) uniform sampler2D normals;
layout(set=0,binding=4) uniform sampler2D albedos;
layout(set=0,binding=5) uniform sampler2D materialEffects;
layout(set=0,binding=6) uniform sampler2D materialAO;
layout(set=0,binding=7) uniform sampler2D screenAO;
layout(set=1,binding=0,std140) uniform SkyData {mat4 inverseViewProjection;vec4 skyCamera;vec4 skySettings;vec4 sunDirectionRadius;vec4 sunRadiance;};
layout(set=1,binding=1) uniform sampler2D skyIrradiance;
#include "ddgi-query.glsl"
#include "sky-mapping.glsl"
void main(){
    vec4 p=texture(positions,uv),n=texture(normals,uv),a=texture(albedos,uv);
    if(p.w==0.||texture(materialEffects,uv).w<0.){colour=vec4(0);return;}
    vec3 N=normalize(n.xyz),V=normalize(ddgiCamera.xyz-p.xyz);
    vec4 gi=ddgiQuery(p.xyz,N,V);vec3 F0=mix(vec3(.04),a.rgb,a.a);
    vec3 F=F0+(max(vec3(1.-n.w),F0)-F0)*pow(1.-max(dot(N,V),0.),5.);
    vec3 diffuse=a.rgb*(1.-F)*(1.-a.a);
    float ao=texture(materialAO,uv).a*texture(screenAO,uv).r;
    vec3 existing=ddgiCamera.w*a.rgb;
    if(skySettings.x>0.)existing+=sampleSkyLut(skyIrradiance,N)*diffuse;
    colour=vec4((gi.rgb*diffuse*ddgiBias.w-existing*(1.-ddgiProbe.w))*ao*gi.a,0.);
}
