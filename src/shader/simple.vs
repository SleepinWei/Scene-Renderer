#version 330 core  
layout (location = 0) in vec3 aPos;  
layout (location = 1) in vec3 aNormal;  
// layout (location = 2) in vec3 aColor;
struct Object{
    vec3 FragPos;  
    vec3 Normal;  
    vec3 Color; 
};
out Object object;

uniform mat4 model;  
uniform mat4 view;  
uniform mat4 projection;  
uniform vec3 objectColor; 

void main()  
{  
    object.FragPos = vec3(model * vec4(aPos, 1.0));  
    // object.Normal = mat3(transpose(inverse(model))) * aNormal;  
    object.Normal = mat3(model) * aNormal;
    // object.Color = aColor; 
    object.Color = objectColor;
    gl_Position = projection * view * model * vec4(aPos, 1.0);  
}
