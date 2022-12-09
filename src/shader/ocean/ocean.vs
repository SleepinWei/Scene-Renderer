#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

// layout(rgba32f,binding=7) uniform sampler2D DisplaceRT;
// layout(rgba32f,binding=8) uniform sampler2D NormalRT;
uniform sampler2D DisplaceRT;
uniform sampler2D NormalRT;

layout(std140,binding=0) uniform VP
{
    mat4 projection;
    mat4 view;
    vec3 viewPos;
};
uniform float OceanLength;
uniform mat4 model;
// uniform mat4 model;
// uniform mat4 view;
// uniform mat4 projection;

out vec3 FragPos;
out vec2 FragTexCoord;

void main()
{
    // FragPos=aPos+texture(DisplaceRT,aTexCoord).xyz);
    FragPos=(model*vec4(aPos+texture(DisplaceRT,aTexCoord).xyz,1.0f)).xyz;
    FragTexCoord=aTexCoord;

    gl_Position = projection*view*vec4(FragPos,1.0f);
}