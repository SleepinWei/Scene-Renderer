#version 430 compatibility

out vec4 FragColor;

struct PointLight{
    // vec3 Color;
    vec3 Color;
    vec3 Position; 
};
layout(std140) uniform PointLightBuffer {
    PointLight pointLights[10];
    int pLightNum; 
};

struct DirectionLight{
    vec3 Color;
    vec3 Position;
    vec3 Direction;
};

layout(std140) uniform DirectionLightBuffer{
    DirectionLight directionLights[10]; 
    int dLightNum;
};

in struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
} object;

uniform vec3 camPos; 

struct Material{
    sampler2D albedo;
    sampler2D metallic; 
    sampler2D normal;
    sampler2D roughness;
    sampler2D ao; 
};
uniform Material material; 

layout(std140) uniform VP{
    mat4 projection;
    mat4 view;
};
void main(){
    // FragColor = projection * view * vec4(object.Position,1.0);
    // FragColor = vec4(texture(material.albedo,object.TexCoords));
    FragColor = vec4(vec3(pLightNum *1.0/ 2),1.0f);
}