#version 330 core
layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec4 gNormal; // mapped normal
layout (location = 2) out vec4 gAlbedoSpec;
layout (location = 3) out vec4 gPBR;

in struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
    vec3 Tangent;
    vec3 Bitangent;
} object;

struct Material{
    sampler2D albedo;
    sampler2D metallic; 
    sampler2D normal;
    sampler2D roughness;
    sampler2D ao; 
    sampler2D height;
};
uniform Material material; 
vec3 getNormalFromMap()
{
    // transform tangent-normals into world-space
    vec3 tangentNormal = texture(material.normal, 
        object.TexCoords).xyz * 2.0 - 1.0;
    tangentNormal = vec3(-tangentNormal.x,tangentNormal.y,tangentNormal.z);

    vec3 Q1  = dFdx(object.Position);
    vec3 Q2  = dFdy(object.Position);
    vec2 st1 = dFdx(object.TexCoords);
    vec2 st2 = dFdy(object.TexCoords);

    vec3 N   = normalize(object.Normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    vec3 actualNormal = TBN * tangentNormal; 
    // if(dot(actualNormal,object.Normal) < 0.0f){
        // actualNormal = -actualNormal;
    // }
    // actualNormal = vec3(-actualNormal.x,actualNormal.y,-actualNormal.z);

    return normalize(actualNormal);
}

void main()
{    
    vec2 uv = object.TexCoords;
    // store the fragment position vector in the first gbuffer texture
    gPosition.rgb = object.Position;

    // also store the per-fragment normals into the gbuffer
    vec3 N = getNormalFromMap();
    gNormal.rgb = normalize(N);
    // and the diffuse per-fragment color
    gAlbedoSpec.rgb = pow(texture(material.albedo, uv).rgb,vec3(2.2f));
    gAlbedoSpec.a = 0.0f; // 是否透光，对草渲染有用
    // store specular intensity in gAlbedoSpec's alpha component

    gPBR.r = texture(material.roughness,uv).g;
    gPBR.g = texture(material.metallic,uv).b;
    gPBR.b = texture(material.ao, uv).r;
    gPBR.a = texture(material.height,uv).r; //a: height
}  