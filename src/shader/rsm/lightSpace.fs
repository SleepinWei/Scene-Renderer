#version 430 core
layout (location=0) out vec4 normal;
layout (location=1) out vec4 worldPos;
layout (location=2) out vec4 flux;

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
uniform float alphaCutoff;
uniform vec3 albedoFactor;

struct Light {
	vec3 Position;
	vec3 Color;
    vec3 Direction;
    float cutOff;
    float outerCutOff;
    int type;
};
uniform Light light;
uniform sampler2D skyIrradiance;
uniform int enableSky;
const float PI = 3.14159265359;

vec3 skyDiffuse(vec3 N) {
    float latitude = asin(clamp(N.y,-1.0,1.0));
    float longitude = atan(-abs(N.x) / (abs(N.z)<1e-6 ? 1e-6 : N.z));
    if (longitude < 0.0) longitude += PI;
    if (N.x < 0.0) longitude = 2.0*PI-longitude;
    float y = .5 + .5*sign(latitude)*sqrt(abs(latitude)/(PI/2.0+1e-4));
    return texture(skyIrradiance,vec2(longitude/(2.0*PI),y)).rgb;
}

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
    if (!gl_FrontFacing) N = -N;
    vec3 basis = Q1*st2.t - Q2*st1.t;
    if (dot(basis,basis) < 1e-12) return N;
    vec3 T = normalize(basis);
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
    // Projected surface footprint in world units, one fragment = one VPL.
    float area = length(cross(dFdx(object.Position), dFdy(object.Position)));
    if (texture(material.albedo, object.TexCoords).a < alphaCutoff) discard;
    worldPos = vec4(object.Position, 1);
    vec3 N = getNormalFromMap();
    normal = vec4(N, 1);
    vec3 incident;
    if (light.type == 1) {
        incident = light.Color * max(dot(N,normalize(-light.Direction)),0.0);
    } else {
        vec3 toLight = light.Position - object.Position;
        float distance2 = max(dot(toLight,toLight),1e-6);
        vec3 L = toLight * inversesqrt(distance2);
        float cone = clamp((dot(L,normalize(-light.Direction))-light.outerCutOff) /
                           max(light.cutOff-light.outerCutOff,1e-5),0.0,1.0);
        incident = light.Color * cone * max(dot(N,L),0.0) / distance2;
    }
    // Deferred IBL multiplies this LUT by albedo directly: it represents E / PI.
    if (enableSky != 0) incident += PI * skyDiffuse(N);
    vec3 albedo = pow(texture(material.albedo, object.TexCoords).rgb, vec3(2.2)) * albedoFactor;
    float metallic = clamp(texture(material.metallic, object.TexCoords).b, 0.0, 1.0);
    // Reflected power = diffuse reflectance * incident irradiance * surface area.
    flux = vec4(albedo * (1.0 - metallic) * incident * area, 1);
}
