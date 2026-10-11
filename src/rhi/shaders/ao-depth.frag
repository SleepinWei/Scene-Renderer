#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=0) out float depth;
#include "ao-parameters.glsl"
layout(set=1,binding=1) uniform sampler2D positionBuffer;
void main() {
    vec4 world=texelFetch(positionBuffer,ivec2(gl_FragCoord.xy),0);
    depth=world.w>0.?max(-(effectView*vec4(world.xyz,1)).z,0.):0.;
}
