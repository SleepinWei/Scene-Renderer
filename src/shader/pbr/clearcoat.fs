#version 430 compatibility

out vec4 FragColor;

struct PointLight{
    // vec3 Color;
    vec3 Color;
    vec3 Position; 
};
layout(std140,binding=1) uniform PointLightBuffer {
    PointLight pointLights[10];
    int pLightNum; 
};

struct DirectionLight{
    vec3 Color;
    vec3 Position;
    vec3 Direction;
};

layout(std140,binding=2) uniform DirectionLightBuffer{
    DirectionLight directionLights[10]; 
    int dLightNum;
};

in struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
    vec3 Tangent;
    vec3 Bitangent;
} object;

uniform vec3 camPos; 

struct Material{
    sampler2D albedo;
    sampler2D metallic; 
    sampler2D normal;
    sampler2D roughness;
    sampler2D ao; 
    sampler2D height;
    sampler2D clearCoatRoughness; 
};
uniform Material material; 
// in mat3 TBN;

const float PI = 3.1415926;

float calculateAtten(vec3 objectPos,vec3 lightPos){
    float distance = length(lightPos - objectPos);
    return 1.0 / (distance * distance);
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

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float V_Kelemen(float LoH){
    return 0.25f / (LoH * LoH);
}

vec3 BRDF(vec3 N, vec3 V,vec3 L){
    vec3 albedo = pow(texture(material.albedo,object.TexCoords).rgb,vec3(2.2));
    // vec3 albedo = vec3(object.TexCoords,1.0);
    float metallic = texture(material.metallic,object.TexCoords).b;
    float roughness = texture(material.roughness,object.TexCoords).g;

    // vec3 objectPosition = object.Position;

    // F0 : plastic, albedo : metallic 
    vec3 F0 = vec3(0.04);
    F0 = mix(F0,albedo,metallic);

    // vec3 Lo = vec3(0.0); 
    // vec3 L = normalize(light.Position - objectPosition);
    vec3 H = normalize(V+L);
    // float attenuation = calculateAtten(objectPosition,light.Position);
    // float attenuation = 1.0f;
    // vec3 radiance = light.Color * attenuation; 

    // cook-tolerance brdf
    float NDF = DistributionGGX(N,H,roughness);
    float G =GeometrySmith(N,V,L,roughness) ;
    vec3 F = fresnelSchlick(max(dot(H,V),0.0),F0);
    // vec3 F = F0;

    vec3 numerator= NDF*G*F;
    float denominator= 4.0 * max(dot(N,V),0.0)*max(dot(N,L),0.0) + 0.0001;
    vec3 specular = numerator / denominator; 
    // vec3 energyCompensation = 1.0 + F0 * (1.0f / numerator.y -1.0f);
    // specular *= energyCompensation;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= (1.0 - metallic);

    // float NdotL = max(dot(N,L),0.0);

    // ------------------- clear Coat model
    float clearCoatRoughness = texture(material.clearCoatRoughness,object.TexCoords).r * 0.5;
    float coatPerceptualRoughness = clamp(clearCoatRoughness,0.089,1.0f);
    float coatRoughness = coatPerceptualRoughness * coatPerceptualRoughness;

    float Dc = DistributionGGX(N,H,coatRoughness);
    // float NoL = max(dot(object.Normal,L),0.0f);
    float LoH = max(dot(L,H),0.0f);
    float Vc = V_Kelemen(LoH);
    float Fc = fresnelSchlick(LoH,vec3(0.4f)).r * 0.5;
    float Frc = (Dc*Vc) *Fc;
    // 

    vec3 Fd = kD * albedo / PI;

    vec3 brdf = (Fd + specular) * (1.0f -Fc) + Frc;

    return brdf;
}

void main(){
    // radiance; 
    vec3 finalColor = vec3(0.0f);
    // point lights
    vec3 N = getNormalFromMap();
    vec3 V = normalize(camPos - object.Position);
    for(int i=0;i<pLightNum;i++){
        PointLight light = pointLights[i];
        vec3 L = normalize(light.Position - object.Position);
        vec3 brdf = BRDF(N,V,L);
        float NdotL = max(dot(N,L),0.0);

        float attenuation = 1.0f;
        vec3 radiance = light.Color * attenuation; 
        finalColor += brdf * radiance * NdotL;
    }
    //directional lights
    for(int i =0;i<dLightNum;i++){
        DirectionLight light = directionLights[i];
        vec3 L = normalize(light.Direction);
        vec3 brdf = BRDF(N,V,L);
        // clear cloat uses geometric normal
        float NdotL = max(dot(object.Normal,L),0.0);

        float attenuation = 1.0f;
        vec3 radiance = light.Color * attenuation; 
        finalColor += brdf * radiance * NdotL;
        // finalColor += computeDirectionShading(object,directionLights[i],material);
    }
    vec3 albedo = pow(texture(material.albedo,object.TexCoords).rgb,vec3(2.2));
    float ao = texture(material.ao,object.TexCoords).r; 
    vec3 ambient = vec3(0.03) * albedo * ao; 

    finalColor += ambient; 

    // HDR tone mapping 
    FragColor = vec4(finalColor,1.0f);

    // vec3 color= 0.5 * getNormalFromMap() + 0.5f;
    // FragColor = vec4(1.0f);
}

