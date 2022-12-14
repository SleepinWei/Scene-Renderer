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
    vec3 Tangent;
    vec3 Bitangent;
} object_tc;


void main(){
    object_tc.Position = vec3(model * vec4(aPos,1.0)) ;
    object_tc.Normal = mat3(transpose(inverse(model))) * aNormal;
    object_tc.TexCoords = TexCoords;
    object_tc.Tangent = Tangent;
    object_tc.Bitangent =Bitangent;

    // gl_Position = projection * view * model * vec4(aPos,1.0);
    // gl_Position = projection * view * vec4(object.Position,1.0);
    // gl_Position = vec4(aPos,1.0);
}