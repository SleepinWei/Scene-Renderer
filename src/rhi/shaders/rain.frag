#version 450
#extension GL_GOOGLE_include_directive : require
#define RAIN_FRAGMENT
#include "rain-parameters.glsl"
layout(location=0) in vec2 dropUV;
layout(location=1) in float visibility;
layout(location=0) out vec4 colour;
layout(set=0,binding=1) uniform sampler2D rainSceneDepth;
void main(){
    float scene=texelFetch(rainSceneDepth,ivec2(gl_FragCoord.xy),0).r;
    if(gl_FragCoord.z>scene||visibility<=0)discard;
    float alpha=exp(-4.*dropUV.x*dropUV.x)*(1.-abs(dropUV.y*2.-1.))*visibility;
    alpha*=smoothstep(0.,.00002,scene-gl_FragCoord.z);
    colour=vec4(rainColour.rgb,alpha*.38);
}
