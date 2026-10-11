#version 450
#extension GL_GOOGLE_include_directive : require
#define DDGI_DEBUG_SET 1
#include "ddgi-debug-parameters.glsl"
#define DDGI_SET 1
#define DDGI_BINDING 3
#include "ddgi-parameters.glsl"
layout(set=1,binding=1,std430) readonly buffer DdgiField {vec4 ddgiField[];};
layout(set=1,binding=2) uniform sampler2D debugPositions;
#include "ddgi-query.glsl"
layout(location=0) in vec2 circleUV;
layout(location=1) flat in int probeIndex;
layout(location=2) flat in vec3 probeCenter;
layout(location=0) out vec4 colour;
void main(){
    float radius2=dot(circleUV,circleUV);if(radius2>1.)discard;
    vec3 toward=normalize(debugEye.xyz-probeCenter);
    vec3 normal=normalize(debugRight.xyz*circleUV.x+debugUp.xyz*circleUV.y+toward*sqrt(max(0.,1.-radius2)));
    vec3 sphere=probeCenter+normal*debugRight.w;
    ivec2 pixel=clamp(ivec2(gl_FragCoord.xy+debugJitter.xy),ivec2(0),textureSize(debugPositions,0)-1);
    vec4 surface=texelFetch(debugPositions,pixel,0);
    bool hidden=surface.w>0.&&dot(surface.xyz-debugEye.xyz,-toward)<dot(sphere-debugEye.xyz,-toward)-.02;
    if(hidden&&debugDisplay.w<.5)discard;
    vec4 state=ddgiField[probeIndex*DDGI_STRIDE];
    vec3 value=state.y<=0.?vec3(1,.8,.04):state.x<.5?vec3(1,.08,.06):vec3(.1,1,.35);
    if(debugDisplay.z<1.5&&state.y>0.&&state.x>.5){
        value=max(ddgiDirectional(probeIndex,normal,0).rgb,vec3(0));
        value=debugJitter.z>.5?1.-exp(-value*debugDisplay.x):clamp(value*debugDisplay.x,0.,1.);
        value=pow(clamp(value,0.,1.),vec3(1./debugDisplay.y));
        // A small white rim makes a dark but valid cache distinguishable from an inactive probe.
        value=mix(value,vec3(.75),smoothstep(.9,1.,radius2));
    }else value*=.45+.55*sqrt(max(0.,1.-radius2));
    colour=vec4(value,hidden?.45:1.);
}
