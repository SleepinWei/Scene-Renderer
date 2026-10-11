#version 450
layout(location=0) in vec2 uv;
layout(set=0,binding=0) uniform sampler2D cloudTransmission;
void main(){gl_FragDepth=texture(cloudTransmission,uv).r;}
