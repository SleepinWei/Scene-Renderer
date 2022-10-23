#version 430 core

layout(triangles) in;
layout(triangle_strip,max_vertices=3) out;

layout(std140,binding=0) uniform VP{
    mat4 projection;
    mat4 view;
};

in struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
} object_gs[];

out Object object;
out mat3 TBN;

uniform mat4 model;

void main(){
    vec3 edge0 = gl_in[1].gl_Position.xyz - gl_in[0].gl_Position.xyz;
    vec3 edge1 = gl_in[2].gl_Position.xyz - gl_in[0].gl_Position.xyz;
    // Lengths of UV differences
    vec2 deltaUV0 = object_gs[1].TexCoords - object_gs[0].TexCoords;
    vec2 deltaUV1 = object_gs[2].TexCoords - object_gs[0].TexCoords;

    // one over the determinant
    float invDet = 1.0 / (deltaUV0.x * deltaUV1.y - deltaUV1.x * deltaUV0.y);

    vec3 tangent = vec3(invDet * (deltaUV1.y * edge0 - deltaUV0.y * edge1));
    vec3 bitangent = vec3(invDet * (-deltaUV1.x * edge0 + deltaUV0.x * edge1));

    vec3 T = normalize(tangent);
    vec3 B = normalize(bitangent);
    vec3 N = normalize(cross(T,B));

    TBN = mat3(T, B, N);
    // TBN is an orthogonal matrix and so its inverse is equal to its transpose
    // mat3 TBN_trans = transpose(TBN);

    for (int i=0; i<3; ++i)
    {
        gl_Position = projection * view * gl_in[i].gl_Position;
        object.Normal = object_gs[i].Normal;
        object.TexCoords = object_gs[i].TexCoords;
        // Change all lighting variables to TBN space
        object.Position = object_gs[i].Position;
        EmitVertex();
    }
    EndPrimitive();
}