#version 450
layout(location=0) in vec2 uv;
layout(location=1) in vec4 tint;
layout(location=0) out vec4 color;
layout(set=1,binding=0) uniform sampler2D guiTexture;
void main(){color=tint*texture(guiTexture,uv);}
