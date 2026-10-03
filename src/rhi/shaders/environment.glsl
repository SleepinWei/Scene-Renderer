layout(set=0,binding=0) uniform sampler2D skyRadianceLut;
layout(set=0,binding=6) uniform sampler2D skyIrradianceLut;
layout(set=0,binding=1,std140) uniform SkyData {mat4 inverseViewProjection;vec4 skyCamera;vec4 skySettings;vec4 sunDirectionRadius;vec4 sunRadiance;};
#include "sky-mapping.glsl"
vec3 environmentAmbient(vec3 p,vec3 N,vec3 V,vec3 F0,float roughness,vec3 albedo,float metallic,float ao){
    if(skySettings.x==0)return cameraAmbient.w*albedo*ao;
    vec3 F=F0+(max(vec3(1.-roughness),F0)-F0)*pow(1.-max(dot(N,V),0),5.);
    vec3 diffuse=sampleSkyLut(skyIrradianceLut,N)*albedo*(1.-F)*(1.-metallic);
    vec3 R=reflect(-V,N),T=normalize(abs(R.y)<.99?cross(R,vec3(0,1,0)):cross(R,vec3(1,0,0))),B=cross(R,T),reflection=vec3(0);
    for(int i=0;i<8;i++){float a=float(i)*2.3999632;vec3 d=normalize(R+(T*cos(a)+B*sin(a))*roughness*roughness*.8);reflection+=sampleSkyLut(skyRadianceLut,d)/8.;}
    return (diffuse+reflection*F)*ao+cameraAmbient.w*albedo*ao;
}
#define RHI_AMBIENT environmentAmbient
