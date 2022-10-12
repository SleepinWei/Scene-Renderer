#version 450 core

layout(location=0) in vec3 aPos;

out vec3 viewDirection;
layout(std140,binding=0) uniform VP{
    mat4 projection;
    mat4 view_;
};

uniform mat4 view;

void main(){
    viewDirection = aPos;
    vec4 temp = projection * view * vec4(aPos,1.0f);
    gl_Position = temp.xyww;
}