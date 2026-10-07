#version 450
#extension GL_GOOGLE_include_directive : require
#include "water-caustics-parameters.glsl"
layout(location=0) in vec4 aPhoton;
layout(set=0,binding=1) uniform sampler2D causticHits;
layout(set=0,binding=2) uniform sampler2D causticFluxes;
layout(location=0) flat out vec3 irradiance;
vec4 photon(sampler2D map,int id){return texelFetch(map,ivec2(id%grid.x,id/grid.x),0);}
void main(){
    vec4 a=photon(causticHits,int(aPhoton.x)),b=photon(causticHits,int(aPhoton.y)),c=photon(causticHits,int(aPhoton.z));
    vec3 position=int(aPhoton.w)==0?a.xyz:int(aPhoton.w)==1?b.xyz:c.xyz;
    vec2 coordinate=position.xz-receiverProjection.xy*(position.y-receiverProjection.z);
    vec2 uv=(coordinate-photonPatch.xy)/photonPatch.z;
    gl_Position=vec4(uv.x*2.-1.,1.-uv.y*2.,0,1);
    // A receiver silhouette is a discontinuity, not a huge connecting face.
    if(receiverProjection.w>.5&&max(max(abs(a.y-b.y),abs(b.y-c.y)),abs(c.y-a.y))>max(1.,sourceSettings.y*6.)){
        gl_Position=vec4(2,2,0,1);irradiance=vec3(0);return;}
    float sourceArea=.5*pow((photonPatch.z+2.*sourceSettings.x)/float(grid.x-1),2.);
    float hitArea=.5*length(cross(b.xyz-a.xyz,c.xyz-a.xyz));
    irradiance=vec3(0);
    if(min(a.w,min(b.w,c.w))<=.5||hitArea<=sourceArea*.002){gl_Position=vec4(2,2,0,1);return;}
    {
        // Integrating each triangle over its receiver area recovers source
        // flux. Regularization bounds singular focal points at finite resolution.
        vec3 energy=(photon(causticFluxes,int(aPhoton.x)).rgb+photon(causticFluxes,int(aPhoton.y)).rgb+photon(causticFluxes,int(aPhoton.z)).rgb)/3.;
        irradiance=energy*min(sourceArea/hitArea,20.);
    }
}
