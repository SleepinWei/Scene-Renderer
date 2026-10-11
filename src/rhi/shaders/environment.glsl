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
    return (diffuse+reflection*F*sunRadiance.w)*ao+cameraAmbient.w*albedo*ao;
}
#define RHI_AMBIENT environmentAmbient

vec3 environmentAmbientShaped(vec3 p,vec3 N,vec3 V,vec3 F0,float roughness,vec3 albedo,float metallic,float ao,vec3 T,vec3 B,vec2 alpha){
    if(skySettings.x==0)return cameraAmbient.w*albedo*ao;
    vec3 F=F0+(max(vec3(1.-roughness),F0)-F0)*pow(1.-max(dot(N,V),0),5.);
    vec3 diffuse=sampleSkyLut(skyIrradianceLut,N)*albedo*(1.-F)*(1.-metallic);
    vec3 R=reflect(-V,N),reflection=vec3(0);
    for(int i=0;i<8;++i){float phi=float(i)*2.3999632;
        vec3 d=normalize(R+(.8*alpha.x*cos(phi))*T+(.8*alpha.y*sin(phi))*B);
        reflection+=sampleSkyLut(skyRadianceLut,d)/8.;
    }
    return (diffuse+reflection*F*sunRadiance.w)*ao+cameraAmbient.w*albedo*ao;
}
#define RHI_AMBIENT_SHAPED environmentAmbientShaped

vec3 environmentLeafAmbient(vec3 N,vec3 tint,float ao){
    return (skySettings.x==0?cameraAmbient.w*tint:(sampleSkyLut(skyIrradianceLut,-N)+vec3(cameraAmbient.w))*tint)*ao;
}
#define RHI_LEAF_AMBIENT environmentLeafAmbient
