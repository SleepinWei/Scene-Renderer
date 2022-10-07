#version 330
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 projection;
uniform mat4 view;

out vec3 normal;

void main(){
    normal = aNormal;
    gl_Position = projection * view * vec4(aPos,1.0);
}
