#version 330 

layout(location=0) in vec3 aPos; 

// uniform mat4 projection;
// uniform mat4 view; 
uniform mat4 model;
layout(std140) uniform VP{
    mat4 projection;
    mat4 view;
};

void main(){
    gl_Position = projection * view * model * vec4(aPos,1.0);
    gl_PointSize = 10;
}