#version 430 core

// layout(location=0) in vec4 aPos;
// layout(location=1) in vec2 TexCoords;
struct Patch{// 1 patch : 16 by 16 grid
    vec4 patchPos[4];
    vec2 patchTex[4];
};
layout(std430,binding=0) buffer Patches{
    Patch patches[];
};

layout(binding=0,std140) uniform VP{
    mat4 projection; 
    mat4 view; 
};
uniform mat4 model;
// uniform sampler2D heightMap;
uniform sampler2D normalMap;

out struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
    vec3 Tangent;
    vec3 Bitangent;
} object;


// 1 node: 8 by 8 patches

void main(){
    int indexChange[6] = {0,1,2,0,2,3};
    int vertexIndex = (gl_VertexID % 6);
    
    Patch p = patches[gl_InstanceID];
    vec4 aPos = p.patchPos[indexChange[vertexIndex]];
    object.Position = vec3(model * aPos);
    // object.TexCoords = p.patchTex[indexChange[vertexIndex]] ;
    object.TexCoords = clamp(aPos.xz * 0.5 + 0.5,0.0f,1.0f);

    // TODO: 需要考虑 y_shift 和 y_scale 
    // vec3 tangentNormal = texture(normalMap,(0.5 * aPos.xz + 0.5)).xyz * 2 -1.0f;
    // tangentNormal = vec3(tangentNormal.x,tangentNormal.z,tangentNormal.y);
    // object.Normal = mat3(transpose(inverse(model))) * tangentNormal;
    object.Normal = vec3(0.0f,1.0f,0.0f);
    // object.Normal.y = object.Normal.y / 256.0f;
    object.Normal = normalize(object.Normal);

    gl_Position = projection * view * model * aPos; 
    // gl_Position = vec4(vec3(gl_VertexID),1.0);
}