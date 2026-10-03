#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in vec4 vertexColor;
layout(set=0,binding=0,std140) uniform GuiProjection{mat4 projection;};
layout(location=0) out vec2 uv;
layout(location=1) out vec4 tint;
void main(){gl_Position=projection*vec4(position,0,1);uv=texCoord;tint=vertexColor;}
