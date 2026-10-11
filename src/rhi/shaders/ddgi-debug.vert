#version 450
#extension GL_GOOGLE_include_directive : require
#include "ddgi-debug-parameters.glsl"
layout(location=0) in vec3 cornerIndex;
layout(location=0) out vec2 circleUV;
layout(location=1) flat out int probeIndex;
layout(location=2) flat out vec3 probeCenter;
void main(){
    probeIndex=int(cornerIndex.z);
    ivec3 grid=ivec3(probeIndex%debugCounts.x,(probeIndex/debugCounts.x)%debugCounts.y,probeIndex/(debugCounts.x*debugCounts.y));
    probeCenter=debugOrigin.xyz+vec3(grid)*debugOrigin.w;
    circleUV=cornerIndex.xy;
    vec3 p=probeCenter+(debugRight.xyz*circleUV.x+debugUp.xyz*circleUV.y)*debugRight.w;
    gl_Position=debugVP*vec4(p,1);
    if(debugCounts.w>=0&&grid.y!=debugCounts.w)gl_Position=vec4(2,2,2,1);
}
