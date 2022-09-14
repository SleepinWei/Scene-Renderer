#version 330 

layout(location=0) in vec3 aPos;

uniform mat4 projection; 
uniform mat4 view;

out float y;

void main(){
    gl_Position = projection * view * vec4(aPos,1.0); 
    y = aPos.y;
}