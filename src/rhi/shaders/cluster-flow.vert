#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(set=0,binding=0,std140) uniform CameraVertex {mat4 viewProjection;};
layout(set=0,binding=1,std140) uniform ObjectData {mat4 model;mat4 normalMatrix;};
layout(location=0) out vec3 diagnosticColor;
void main(){gl_Position=viewProjection*model*vec4(position,1);diagnosticColor=color;}
