#version 450
#extension GL_GOOGLE_include_directive : require
#include "sky-mapping.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std140) uniform UnderwaterParameters {
    mat4 inverseVP;mat4 coastInverse;mat4 coastModel;
    vec4 coastPatch;vec4 coastPreviousPatch;vec4 coastFeatures;vec4 coastBedInfo;
    vec4 camera;vec4 waves;vec4 absorb;vec4 scatter;vec4 direction;vec4 diffuse;vec4 controls;vec4 diving;vec4 shaftPatch;vec4 shaftProjection;
};
layout(set=0,binding=1) uniform sampler2D sunShaftField;
layout(set=1,binding=0) uniform sampler2D eyeScene;
layout(set=1,binding=1) uniform sampler2D eyePosition;
layout(set=1,binding=2) uniform sampler2D eyeNormal;
layout(set=1,binding=3) uniform sampler2D eyeDisplace;
layout(set=1,binding=4) uniform sampler2D eyeShore;
layout(set=1,binding=5) uniform sampler2D eyeMask;
layout(set=1,binding=6) uniform sampler2D eyeSky;
layout(set=1,binding=7) uniform sampler2D eyeOriginal;
#include "water-waves.glsl"
#include "water-coast.glsl"
#include "water-shadow.glsl"
#define WATER_MEDIUM_VISIBILITY(point) waterSunVisibility(point,int(controls.w))
#include "water-shafts-sample.glsl"
#define WATER_MEDIUM_SOLAR_FLUX(point) waterSunShaftFlux(point,sunShaftField,shaftPatch,shaftProjection,diving.z)
#define WATER_MEDIUM_STEPS int(diving.w)
#define WATER_PARTICLE_DENSITY diving.y
#define WATER_PARTICLE_TIME diving.x
#include "water-medium.glsl"
void main() {
    if(!waterEyeInside(camera.xyz,eyeDisplace,eyeShore,eyeMask,waves,controls.x))discard;
    vec4 farPoint=inverseVP*vec4(uv*vec2(2,-2)+vec2(-1,1),1,1);
    vec3 ray=normalize(farPoint.xyz/farPoint.w-camera.xyz);
    vec2 capturedUV=camera.w>.5?vec2(uv.x/camera.w,uv.y):uv;
    vec3 position=texture(eyePosition,uv).xyz,N=texture(eyeNormal,uv).xyz;
    bool geometry=dot(N,N)>.25;
    float distance=geometry?min(length(position-camera.xyz),absorb.w):absorb.w;
    // Clip the view segment at the first wave-surface exit. Surface shading
    // later replaces these pixels and integrates its own eye leg once.
    float old=0.;
    for(int i=1;i<=16;++i){
        float t=distance*float(i)/16.;vec3 point=camera.xyz+ray*t;
        if(point.y>=waterMediumHeight(point.xz,eyeDisplace,eyeShore,waves)){
            float low=old,high=t;
            for(int j=0;j<6;++j){float mid=(low+high)*.5;vec3 q=camera.xyz+ray*mid;
                if(q.y<waterMediumHeight(q.xz,eyeDisplace,eyeShore,waves))low=mid;else high=mid;}
            distance=(low+high)*.5;break;
        }
        old=t;
    }
    // Outside the finite water grid the path ends at its XZ domain boundary.
    for(int axis=0;axis<2;++axis){int a=axis==0?0:2;
        if(abs(ray[a])>1e-7)distance=min(distance,max((sign(ray[a])*controls.x-camera[a])/ray[a],0.));}
    vec3 sky=controls.y>.5?sampleSkyLut(eyeSky,vec3(0,1,0)):vec3(0);
    bool submergedGeometry=geometry&&position.y<waterMediumHeight(position.xz,eyeDisplace,eyeShore,waves)-.002;
    vec3 background=submergedGeometry?texture(eyeScene,capturedUV).rgb:texture(eyeOriginal,uv).rgb;
    vec3 result=waterMediumTransport(background,camera.xyz,ray,distance,
        normalize(-direction.xyz),diffuse.xyz,sky,absorb.xyz,scatter.xyz,scatter.w,controls.z,eyeDisplace,eyeShore,waves);
    color=vec4(max(result,vec3(0)),1);
}
