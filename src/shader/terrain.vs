#version 430

layout(location=0) in vec4 aPos;
layout(location=1) in vec2 TexCoords;

layout(binding=0,std140) uniform VP{
    mat4 projection; 
    mat4 view; 
};
uniform mat4 model;

void main(){
    gl_Position = projection * view * model * vec4(aPos.xyz,1.0); 
    // gl_Position = vec4(vec3(gl_VertexID),1.0);
}