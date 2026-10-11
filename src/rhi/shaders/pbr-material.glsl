layout(set=1,binding=0,std140) uniform MaterialData { vec4 albedoAlpha; vec4 factors; vec4 emissiveNormal; };
layout(set=1,binding=1) uniform sampler2D albedoMap;
layout(set=1,binding=2) uniform sampler2D normalMap;
layout(set=1,binding=3) uniform sampler2D metallicMap;
layout(set=1,binding=4) uniform sampler2D roughnessMap;
layout(set=1,binding=5) uniform sampler2D aoMap;
#include "material-sampling.glsl"
#include "shoreline-material.glsl"
// Native vertices carry source corner frames; zero selects a UV derivative frame.
layout(location=3) in vec3 worldTangent;
layout(location=4) in vec3 worldBitangent;
layout(location=5) in vec3 normalTangent;
layout(location=6) in vec3 normalBitangent;
layout(location=7) in vec3 rawWorldNormal;
vec4 mappedLobes(){
    vec4 map=isVirtualMaterial()?vec4(1):texture(specialMap,uv);
    vec4 lobes=vec4(materialLobes.x,max(map.r*materialLobes.y,.089),clamp(map.g*materialLobes.z,-.95,.95),materialLobes.w);
    if((materialFeatures.x&32)!=0)lobes.w=2.+clamp(materialLobes.w*map.a,0.,1.);
    // Model occupies the upper part of coat roughness, which is [0,1].
    if(materialSpecularShape.z>0){lobes.y+=4.*materialSpecularShape.z;lobes.z=materialSpecularShape.x;}
    return lobes;
}
void mappedFrame(vec3 N,out vec3 T,out vec3 B){
    T=worldTangent;B=worldBitangent;
    if(dot(T,T)<1e-12 || dot(B,B)<1e-12){
        vec3 q1=dFdx(worldPosition),q2=dFdy(worldPosition);vec2 a=dFdx(uv),b=dFdy(uv);
        float determinant=a.x*b.y-a.y*b.x;
        if(abs(determinant)>1e-8){T=(q1*b.y-q2*a.y)/determinant;B=(q2*a.x-q1*b.x)/determinant;}
        else {T=vec3(0);B=vec3(0);}
    }
    T-=N*dot(T,N);
    if(dot(T,T)<1e-12){T=normalize(cross(abs(N.y)<.99?vec3(0,1,0):vec3(1,0,0),N));B=cross(N,T);return;}
    T=normalize(T);B=cross(N,T)*(dot(cross(N,T),B)<0?-1.:1.);
}
vec3 mappedTangent(vec3 N){
    vec3 T,B;mappedFrame(N,T,B);
    if(materialSpecularShape.z>0)T=T*cos(materialSpecularShape.y)+B*sin(materialSpecularShape.y);
    return T;
}
vec3 mappedNormal() {
    vec3 N = normalize(worldNormal);
    if(hasShoreline())return beachNormal(N);
    if(materialSpecularShape.z>0 && dot(normalTangent,normalTangent)>1e-12 && dot(normalBitangent,normalBitangent)>1e-12){
        vec3 sampled=sampleMaterial(normalMap,uv).xyz*2.-1.;sampled.xy*=emissiveNormal.w;
        sampled.z=mix(1.,sampled.z,clamp(emissiveNormal.w,0.,1.));
        vec3 mapped=normalTangent*sampled.x+normalBitangent*sampled.y+rawWorldNormal*sampled.z;
        return dot(mapped,mapped)>1e-12?normalize(mapped):N;
    }
    vec3 q1 = dFdx(worldPosition), q2 = dFdy(worldPosition);
    vec2 t1 = dFdx(uv), t2 = dFdy(uv);
    float determinant = t1.x * t2.y - t1.y * t2.x;
    if (abs(determinant) < 1e-8) return N;
    vec3 T = (q1 * t2.y - q2 * t1.y) / determinant;
    vec3 B = (-q1 * t2.x + q2 * t1.x) / determinant;
    if (dot(T,T) < 1e-10 || dot(B,B) < 1e-10) return N;
    T -= N * dot(T,N);B -= N * dot(B,N);
    if (dot(T,T) < 1e-10 || dot(B,B) < 1e-10) return N;
    T = normalize(T);B = normalize(B);
    vec3 sampleNormal = sampleMaterial(normalMap, uv).xyz * 2 - 1;
    sampleNormal.xy *= emissiveNormal.w;
    vec3 mapped = mat3(T,B,N) * sampleNormal;
    return dot(mapped,mapped) < 1e-10 ? N : normalize(mapped);
}
