#version 410 core 

in struct Object{
    vec3 FragPos; 
    vec3 Normal;
} object; 

out vec4 FragColor; 
uniform vec3 camPos; 
uniform float ratio; // refraction rate (<1)


void main(){
    vec3 viewDir = normalize(object.FragPos - camPos);
    vec3 norm = normalize(object.Normal);
    vec3 refract_dir = refract(viewDir, norm, ratio);

}