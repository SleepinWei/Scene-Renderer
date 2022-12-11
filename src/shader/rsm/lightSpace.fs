#version 430 core
layout (location=0) out vec3 normal;
layout (location=1) out vec3 worldPos;
layout (location=2) out vec3 flux;

in struct Object{
    vec3 Position;
    vec3 Normal;
    vec2 TexCoords;
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

struct Light {
	vec3 Position;
	vec3 Color;
};
uniform Light light;

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
    worldPos = object.Position;
    vec3 N = getNormalFromMap();
    normal = normalize(N);

    vec3 lightDir = normalize(light.Position - worldPos);
	float diff = max(0.0, dot(normal, lightDir));
	flux = diff*texture(material.albedo, object.TexCoords).xyz*light.Color;
}