#version 330

in float y; 

uniform float shift; 
uniform float scale; 

out vec3 FragColor;

void main(){
    FragColor = vec3((y+shift) / scale / 256.0f);
}