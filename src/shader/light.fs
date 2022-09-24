#version 330 
out vec4 FragColor; 

uniform float lightColor; 

void main(){
    FragColor = vec4(lightColor,lightColor,lightColor,1.0);
}