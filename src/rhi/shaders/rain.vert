#version 450
#extension GL_GOOGLE_include_directive : require
#include "rain-parameters.glsl"
layout(location=0) in vec2 corner;
layout(location=0) out vec2 dropUV;
layout(location=1) out float visibility;
uint hash(uint v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
float random(uint v){return float(hash(v)&0xffffffu)/16777216.;}
void main(){
    uint id=uint(gl_VertexIndex)/6u;float time=rainCameraTime.w;
    vec3 head=rainCameraTime.xyz+vec3((random(id*3u+1u)-.5)*40.,(fract(random(id*3u+2u)-time*9./24.)-.5)*24.,(random(id*3u+3u)-.5)*40.);
    vec3 velocity=vec3(rainWindRate.x*.2,-9.,rainWindRate.y*.2);
    head.xz=rainCameraTime.xz+mod(head.xz-rainCameraTime.xz+vec2(rainScreen.w,rainColour.w)+20.,40.)-20.;
    vec4 a=rainVP*vec4(head,1),b=rainVP*vec4(head-velocity*.045,1);
    dropUV=vec2(corner.x,corner.y);visibility=0;
    if(a.w<=.1||b.w<=.1){gl_Position=vec4(2,2,2,1);return;}
    vec2 delta=(b.xy/b.w-a.xy/a.w)*rainScreen.xy;
    vec2 side=length(delta)>.001?normalize(vec2(-delta.y,delta.x)):vec2(1,0);
    gl_Position=mix(a,b,corner.y);gl_Position.xy+=side*corner.x*rainScreen.z/rainScreen.xy*gl_Position.w;
    visibility=(1.-smoothstep(14.,23.,length(head-rainCameraTime.xyz)))*rainWindRate.w;
}
