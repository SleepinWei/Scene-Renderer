#version 450
layout(location=0) flat in vec3 irradiance;
layout(location=0) out vec4 color;
void main(){color=vec4(irradiance,1);}
