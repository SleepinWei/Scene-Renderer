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

out struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
} object;
// out mat3 TBN;


void main(){
    object.Position = vec3(model * vec4(aPos,1.0)) ;
    object.Normal = normalize(mat3(transpose(inverse(model))) * aNormal);
    object.TexCoords = TexCoords;

    gl_Position = projection * view * model * vec4(aPos,1.0);
    // gl_Position = projection * view * vec4(object_gs.Position,1.0);
    // gl_Position = model * vec4(aPos,1.0);
}