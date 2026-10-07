#version 450
#extension GL_GOOGLE_include_directive : require
#include "water-shafts-parameters.glsl"
layout(location=0) in vec4 aPhoton;
layout(set=0,binding=1) uniform sampler2D shaftSources;
layout(set=0,binding=2) uniform sampler2D shaftDirections;
layout(location=0) flat out vec3 irradiance;
const float shaftDepths[8]=float[8](0.,1.5,3.,5.,8.,12.,20.,32.);
vec4 photon(sampler2D map,int id){return texelFetch(map,ivec2(id%grid.x,id/grid.x),0);}
vec3 intersectSlice(vec4 origin,vec4 ray){float depth=waves.x-shaftDepths[grid.z];return origin.xyz+ray.xyz*max((depth-origin.y)/min(ray.y,-.05),0.);}
void main(){
    vec4 a=photon(shaftSources,int(aPhoton.x)),b=photon(shaftSources,int(aPhoton.y)),c=photon(shaftSources,int(aPhoton.z));
    vec4 da=photon(shaftDirections,int(aPhoton.x)),db=photon(shaftDirections,int(aPhoton.y)),dc=photon(shaftDirections,int(aPhoton.z));
    vec3 pa=intersectSlice(a,da),pb=intersectSlice(b,db),pc=intersectSlice(c,dc);
    vec3 position=int(aPhoton.w)==0?pa:int(aPhoton.w)==1?pb:pc;
    vec2 coordinate=position.xz-shaftProjection.xy*(position.y-waves.x),uv=(coordinate-shaftPatch.xy)/shaftPatch.z;
    gl_Position=vec4(uv.x*2.-1.,1.-uv.y*2.,0,1);irradiance=vec3(0);
    vec2 e1=pb.xz-pa.xz,e2=pc.xz-pa.xz;float area=abs(e1.x*e2.y-e1.y*e2.x);
    float sourceArea=shaftSettings.z*shaftSettings.z;
    if(min(a.w,min(b.w,c.w))<.5||area<sourceArea*.002){gl_Position=vec4(2,2,0,1);return;}
    irradiance=vec3((da.w+db.w+dc.w)/3.*min(sourceArea/area,16.));
}
