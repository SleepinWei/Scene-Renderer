#version 430 
layout(location=0) in vec3 aPos; 
layout(location=1) in vec3 aNormal; 
layout(location=2) in vec2 TexCoords; 
layout(location=3) in vec3 Tangent;
layout(location=4) in vec3 Bitangent;

uniform mat4 model; 
layout(std140,binding=0) uniform VP{
    mat4 projection;
    mat4 view;
};

out float Depth; 

void main(){
    vec4 CamSpace = view * model * vec4(aPos,1.0f);
    Depth = CamSpace.z/ CamSpace.w;
    gl_Position = projection * view * model * vec4(aPos,1.0f);
}