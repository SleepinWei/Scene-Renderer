#version 430 core

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gPBR;

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

uniform vec3 camPos; 


// global variable
vec3 albedo;
float metallic;
float roughness;

const float PI = 3.1415926;

float calculateAtten(vec3 objectPos,vec3 lightPos){
    float distance = length(lightPos - objectPos);
    return 1.0 / (distance * distance);
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

vec3 BRDF(vec3 N, vec3 V,vec3 L){
    // F0 : plastic, albedo : metallic 
    vec3 F0 = vec3(0.04);
    F0 = mix(F0,albedo,metallic);

    // vec3 Lo = vec3(0.0); 
    // vec3 L = normalize(light.Position - objectPosition);
    vec3 H = normalize(V+L);
    // float attenuation = calculateAtten(object.Position,light.Position);
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
    // Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    vec3 brdf = kD * albedo / PI + specular;

    return brdf;
}

vec3 shading(){
    // radiance; 
    vec3 finalColor = vec3(0.0f);
    // point lights
    vec3 N = texture(gNormal,TexCoords).rgb;
    vec3 Position = texture(gPosition,TexCoords).rgb;
    vec3 V = normalize(camPos - Position);

    // global
    albedo = texture(gAlbedoSpec,TexCoords).rgb;
    metallic = texture(gPBR,TexCoords).g;
    roughness = texture(gPBR,TexCoords).r;

    float isGrass = texture(gAlbedoSpec,TexCoords).a;
 
    for(int i=0;i<pLightNum;i++){
        PointLight light = pointLights[i];
        vec3 L = normalize(light.Position - Position);
        vec3 N_ = N;
        if(isGrass>0.5f && dot(N,L)<0.0f){
            N_ = -N;
        }
        vec3 brdf = BRDF(N_,V,L);
        float NdotL = max(dot(N_,L),0.0);

        float attenuation = 1.0f;
        vec3 radiance = light.Color * attenuation; 
        finalColor += brdf * radiance * NdotL;
    }
    //directional lights
    for(int i =0;i<dLightNum;i++){
        DirectionLight light = directionLights[i];
        vec3 L = normalize(light.Direction);
        vec3 N_ = N;
        if(isGrass>0.5f && dot(N,L)<0.0f){
            // if is grass, back is still bright;
            N_ = -N;
        }

        vec3 brdf = BRDF(N_,V,L);
        float NdotL = max(dot(N_,L),0.0);

        float attenuation = 1.0f;
        vec3 radiance = light.Color * attenuation; 
        finalColor += brdf * radiance * NdotL;
        // finalColor += computeDirectionShading(object,directionLights[i],material);
    }
    vec3 albedo = texture(gAlbedoSpec,TexCoords).rgb;
    float ao = texture(gPBR,TexCoords).a; 
    vec3 ambient = vec3(0.03) * albedo * ao; 

    finalColor += ambient; 

    return finalColor;
}

void main(){
    vec3 color = shading();

    // color correction
    
    FragColor = vec4(color,1.0f);
}