#version 410 core
layout(triangles, equal_spacing, ccw) in;

struct Material{
    sampler2D albedo;
    sampler2D metallic; 
    sampler2D normal;
    sampler2D roughness;
    sampler2D ao; 
    sampler2D height;
};

uniform Material material;

uniform mat4 model;
layout(std140) uniform VP{
    mat4 projection;
    mat4 view;
};

out struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
} object;

in Object object_ts[];

void main()
{
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;
    float w = gl_TessCoord.z;

    object.TexCoords = object_ts[0].TexCoords * u + object_ts[1].TexCoords * v + object_ts[2].TexCoords * w; 
    object.Normal = object_ts[0].Normal * u + object_ts[1].Normal * v + object_ts[2].Normal * w; 
    object.Position = object_ts[0].Position * u + object_ts[1].Position * v + object_ts[2].Position * w;

    float height = texture(material.height,object.TexCoords).r;
    float heightScale = 0.02f; 
    object.Position = object.Position + normalize(object.Normal) * height * heightScale;
    gl_Position = projection * view * vec4(object.Position,1.0); 
}