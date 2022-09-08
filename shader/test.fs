#version 330 
out vec4 FragColor;

in vec3 fragColor;
in vec3 normal;

void main(){
    FragColor = vec4(fragColor,1.0);
}