#version 430
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 TexCoords;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;
out struct Object{
    vec3 Position;
    vec3 Normal;
    vec2 TexCoords;
} object;


void main(){
    object.Position = vec3(model * vec4(aPos,1.0)) ;
    object.Normal = normalize(mat3(transpose(inverse(model))) * aNormal);
    object.TexCoords = TexCoords;

    gl_Position = lightSpaceMatrix * model * vec4(aPos,1.0);
}