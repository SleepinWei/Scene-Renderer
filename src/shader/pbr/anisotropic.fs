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
    sampler2D anisotropy;
};
uniform Material material; 
// in mat3 TBN;

const float PI = 3.1415926;

float calculateAtten(vec3 objectPos,vec3 lightPos){
    float distance = length(lightPos - objectPos);
    return 1.0 / (distance * distance);
}

void getNormalFromMap(out vec3 N_out, out vec3 T_out, out vec3 B_out)
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

    N_out = normalize(actualNormal);
    B_out = normalize(cross(N_out,T));
    T_out = normalize(cross(B_out,N_out));
}

float D_GGX_Anisotropic(float at, float ab,float ToH,float BoH,float NoH)
{
    float a2 = at * ab;
    // float NoH = max(dot(N,H),0.0f);
    // float ToH = max(dot(T,H),0.0f);
    // float BoH = max(dot(B,H),0.0f);

    vec3 d = vec3(ab * ToH, at * BoH, a2 * NoH);
    float d2 = dot(d, d);
    float b2 = a2 / d2;
    return a2 * b2 * b2 * (1.0 / PI);
}

float D_GGX(float roughness, float NoH, const vec3 h) {
    // Walter et al. 2007, "Microfacet Models for Refraction through Rough Surfaces"

    // In mediump, there are two problems computing 1.0 - NoH^2
    // 1) 1.0 - NoH^2 suffers floating point cancellation when NoH^2 is close to 1 (highlights)
    // 2) NoH doesn't have enough precision around 1.0
    // Both problem can be fixed by computing 1-NoH^2 in highp and providing NoH in highp as well

    // However, we can do better using Lagrange's identity:
    //      ||a x b||^2 = ||a||^2 ||b||^2 - (a . b)^2
    // since N and H are unit vectors: ||N x H||^2 = 1.0 - NoH^2
    // This computes 1.0 - NoH^2 directly (which is close to zero in the highlights and has
    // enough precision).
    // Overall this yields better performance, keeping all computations in mediump
    float oneMinusNoHSquared = 1.0 - NoH * NoH;

    float a = NoH * roughness;
    float k = roughness / (oneMinusNoHSquared + a * a);
    float d = k * k * (1.0 / PI);
    return d;
}

float V_SmithGGXCorrelated_Anisotropic(float at, float ab, float ToV, float BoV,
        float ToL, float BoL, float NoV, float NoL) {
    // Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
    // TODO: lambdaV can be pre-computed for all the lights, it should be moved out of this function
    float lambdaV = NoL * length(vec3(at * ToV, ab * BoV, NoV));
    float lambdaL = NoV * length(vec3(at * ToL, ab * BoL, NoL));
    float v = 0.5 / (lambdaV + lambdaL);
    return v;
}

float V_SmithGGXCorrelated(float roughness, float NoV, float NoL) {
    // Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
    float a2 = roughness * roughness;
    // TODO: lambdaV can be pre-computed for all the lights, it should be moved out of this function
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    float v = 0.5 / (lambdaV + lambdaL);
    // a2=0 => v = 1 / 4*NoL*NoV   => min=1/4, max=+inf
    // a2=1 => v = 1 / 2*(NoL+NoV) => min=1/4, max=+inf
    // clamp to the maximum value representable in mediump
    return v;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 BRDF(vec3 N, vec3 V,vec3 L,vec3 T,vec3 B){
    vec3 albedo = pow(texture(material.albedo,object.TexCoords).rgb,vec3(2.2));
    // vec3 albedo = vec3(object.TexCoords,1.0);
    float metallic = texture(material.metallic,object.TexCoords).r;
    float roughness = texture(material.roughness,object.TexCoords).r;
    float anisotropy = texture(material.anisotropy,object.TexCoords).r;
    float at = max(roughness * (1.0 + anisotropy),0.001);
    float ab = max(roughness * (1.0 - anisotropy),0.001);

    // vec3 objectPosition = object.Position;

    // F0 : plastic, albedo : metallic 
    vec3 F0 = vec3(0.04);
    F0 = mix(F0,albedo,metallic);

    vec3 H = normalize(V+L);

    float NoH = clamp(dot(N,H),0.0f,1.0f);
    float ToH = dot(T,H);
    float BoH = dot(B,H);
    float NDF = D_GGX_Anisotropic(at,ab,ToH,BoH,NoH);
    // float NDF = D_GGX(roughness,NoH,H);

    float ToV = clamp(dot(T,V),0.0f,1.0f);
    float BoV = max(dot(B,V),0.0f);
    float ToL = max(dot(T,L),0.0f);
    float BoL = max(dot(B,L),0.0f);
    float NoV = max(dot(N,V),0.0f);
    float NoL = max(dot(N,L),0.0f);
    // float V_Smith = V_SmithGGXCorrelated_Anisotropic(at,ab,ToV,BoV,ToL,BoL,NoV,NoL);
    float V_Smith = V_SmithGGXCorrelated(roughness,NoV,NoL);

    float LoH = max(dot(L,H),0.0f);
    vec3 F = fresnelSchlick(LoH,F0);
    // vec3 F = F0;

    vec3 Fr = (NDF*V_Smith)*F;
    // vec3 energyCompensation = 1.0 + F0 * (1.0f / numerator.y -1.0f);
    // specular *= energyCompensation;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= (1.0 - metallic);

    // float NdotL = max(dot(N,L),0.0);
    // Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    vec3 Fd =  kD * albedo / PI;
    vec3 brdf = Fd + Fr;

    return brdf;
}

void main(){
    // radiance; 
    vec3 finalColor = vec3(0.0f);
    // point lights
    // vec3 N = getNormalFromMap();
    vec3 T,B,N;
    getNormalFromMap(N,T,B);
    vec3 V = normalize(camPos - object.Position);
    for(int i=0;i<pLightNum;i++){
        PointLight light = pointLights[i];
        vec3 L = normalize(light.Position - object.Position);
        vec3 brdf = BRDF(N,V,L,T,B);
        float NdotL = max(dot(N,L),0.0);

        float attenuation = 1.0f;
        vec3 radiance = light.Color * attenuation; 
        finalColor += brdf * radiance * NdotL;
    }
    //directional lights
    for(int i =0;i<dLightNum;i++){
        DirectionLight light = directionLights[i];
        vec3 L = normalize(light.Direction);
        vec3 brdf = BRDF(N,V,L,T,B);
        float NdotL = max(dot(N,L),0.0);

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

