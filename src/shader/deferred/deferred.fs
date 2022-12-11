#version 430 core

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gPBR;
uniform bool enableShadow;


/********variables for shadows*******/
#define MAX_LIGHT_NUM 10
#define MAX_CASCADED_LEVEL 5

// uniform mat4 model; 
layout(std140,binding=0) uniform VP{
    mat4 projection;
    mat4 view;
    vec3 camPos;
};
//uniform vec3 viewPos; // position of camera .. world space  
//VIEW POS is CAMPOS
uniform float far_plane;
uniform samplerCube shadow_cubes[MAX_LIGHT_NUM];
uniform sampler2DArray shadow_maps[MAX_LIGHT_NUM];
uniform float cascaded_distances[MAX_CASCADED_LEVEL];
uniform int cascaded_levels;

// PENDING UBO
layout(std140,binding=5) uniform light_space_matrices_buffer
{
    mat4 light_space_matrices[MAX_CASCADED_LEVEL*MAX_LIGHT_NUM]; //directional light matrices
};
/*******/

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


struct SpotLight{
    vec3 Color;
    float cutOff;
    vec3 Position;
    float outerCutOff;
    vec3 Direction;
};

layout(std140,binding=3) uniform SpotLightBuffer{
    SpotLight SpotLights[10];
    int sLightNum;
};

// uniform vec3 camPos;
// global variable
vec3 albedo;
float metallic;
float roughness;

const float PI = 3.1415926;

/**** functions for shadows****/
int get_layer(float depth_view_space)  //for directional light
{
    int re=-1;
    for(int i=0;i<cascaded_levels;i++)
    {
        if(depth_view_space<cascaded_distances[i])
        {
            re=i;
            break;
        }
    }
    if(re==-1)
        re=cascaded_levels;
    
    return re;
}

float get_light_block_distance(vec2 coords_xy,float z,int layer,int order)
{
    float object_depth_calculated= z;
    // vec3 texel_size3d=textureSize(shadow_maps[order],0);
    // vec2 texel_size=1.0/texel_size3d.xy;
    vec2 texel_size = vec2(1.0f/(2048.0f));

    int scale=20-layer;  
    // the more the layer is , the less times we search on the shadow map
    // for one texel represents more points in world space
    int cnt=0;
    float result=0.0f;
    for(int i=-scale;i<=scale;i++)
    {
        for(int j=-scale;j<=scale;j++)
        {
            float depth = texture(shadow_maps[0],vec3(coords_xy+vec2(i,j)*texel_size,layer)).r;
            if(depth <object_depth_calculated-0.05f)
            {
                result+=texture(shadow_maps[0],vec3(coords_xy+vec2(i,j)*texel_size,layer)).r;
                cnt++;
            }
        }
    }
    
    if(cnt!=0)
        return result/cnt;
    else
        return object_depth_calculated;

}

float calculate_directional_shadow(vec3 fragPosWorldSpace,vec3 normal,int order)
{
    float re=0.0f;

    vec4 fragPosViewSpace =view* vec4(fragPosWorldSpace,1.0);
    float depth_value_view_space=abs(fragPosViewSpace.z);

    int layer=get_layer(depth_value_view_space);

    vec4 fragPosLightSpace= light_space_matrices[0*(cascaded_levels+1)+layer]*vec4(fragPosWorldSpace,1.0);

    //perform perspective divide
    vec3 proj_coords=fragPosLightSpace.xyz/fragPosLightSpace.w;
    proj_coords=proj_coords*0.5+0.5;

    // this value is calculated by matrix, here we will compare it with the
    float depth_value_calculated=proj_coords.z;

    // if the z in the light space is out of the far_plane 
    if(depth_value_calculated>1.0f)
    {
        re=0.0;
        return re;
    }

    //calculate bias
    float bias=max(0.02*(1.0-dot(normal,normalize(-directionLights[0].Direction))),0.01);

    //modify bias according to which frustum
    float modify_factor = 0.5f;
    if(layer==cascaded_levels)
        bias*=1.0f/(far_plane*modify_factor);
    else
        bias*=1.0f/(cascaded_distances[layer]*modify_factor);

    //PCSS
    float light_block_dis=get_light_block_distance(proj_coords.xy,depth_value_calculated,layer,0);
    float block_object_dis=depth_value_calculated-light_block_dis;
    int filter_kernel_size=int(1.0*block_object_dis/light_block_dis*10);//suppose 10 is the width;
    // debug 

    filter_kernel_size=filter_kernel_size>10?10:filter_kernel_size;
    filter_kernel_size=filter_kernel_size<1?1:filter_kernel_size;

    vec2 texel_size = vec2(1.0f / (2048.0f));

    int cnt=0;
    for(int i=-filter_kernel_size;i<=filter_kernel_size;i++)
        for(int j=-filter_kernel_size;j<=filter_kernel_size;j++)
        {
            float texture_depth=texture(shadow_maps[0],vec3(proj_coords.xy+vec2(i,j)*texel_size,layer)).r;
            re+=(depth_value_calculated-bias)>texture_depth? 1.0:0.0;
            cnt++;
        }

    //for debug
    // int cnt = int(light_block_dis);

    if(cnt!=0)
    {
        re=re/cnt;
        return re;
    }
    else
        return 0.0f;

}

vec3 grid_sample_directions[20] = vec3[](
    vec3(1, 1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1, 1,  1), 
    vec3(1, 1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
    vec3(1, 1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1, 1,  0),
    vec3(1, 0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1, 0, -1),
    vec3(0, 1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0, 1, -1)
);
float calculate_point_shadow(vec3 fragPosWorldSpace,vec3 normal,int order)
{
    float re=0.0f;
    vec3 frag2light=fragPosWorldSpace-pointLights[0].Position;
    float depth_calculated=length(frag2light);
    int num_samples=20; // 20 representative directions

    float view_object_dis=length(camPos-fragPosWorldSpace);
    float bias=max(0.05*(1.0-dot(normal,fragPosWorldSpace-pointLights[0].Position)),0.05);
    // float bias = 0.15f;
    float radius = (1.0f + (view_object_dis / far_plane))/25.0f;

    for(int i=0;i<num_samples;i++)
    {
        float texture_depth=texture(shadow_cubes[0],frag2light+grid_sample_directions[i]*radius).r;
        texture_depth*=far_plane; // real distance in world space
        if(texture_depth + bias<depth_calculated)
            re+=1.0f;
    }
    return re/num_samples;
}


/************/


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

        // float attenuation = 1.0f;
        float attenuation =min(1.0f,calculateAtten(Position,light.Position));
        vec3 radiance = light.Color * attenuation;

        float shadow_factor;
        if(enableShadow)
            shadow_factor=calculate_point_shadow(Position,N,i);
        else
            shadow_factor = 0.0f;
        finalColor +=(1.0f-shadow_factor)*brdf * radiance * NdotL;
    }
    //directional lights
    for(int i =0;i<dLightNum;i++){
        DirectionLight light = directionLights[i];
        vec3 L = normalize(-light.Direction);
        vec3 N_ = N;
        if(isGrass>0.5f && dot(N,L)<0.0f){
            // if is grass, back is still bright;
            N_ = -N;
        }

        vec3 brdf = BRDF(N_,V,L);
        float NdotL = max(dot(N_,L),0.0);

        float attenuation = 1.0f;
        vec3 radiance = light.Color * attenuation; 

        float shadow_factor;
        if(enableShadow)
            shadow_factor = calculate_directional_shadow(Position,N,i);
        else
            shadow_factor = 0.0f;
        // float shadow_factor = 0.0f;
        finalColor +=(1.0-shadow_factor)* brdf * radiance * NdotL;
        // finalColor += computeDirectionShading(object,directionLights[i],material);
    }
    //spotlight
    for(int i =0;i<sLightNum;i++){
        SpotLight light = SpotLights[i];
        vec3 L = normalize(light.Position - Position);
        vec3 N_ = N;
        if(isGrass>0.5f && dot(N,L)<0.0f){
            // if is grass, back is still bright;
            N_ = -N;
        }

        vec3 brdf = BRDF(N_,V,L);
        float NdotL = max(dot(N_,L),0.0);

        // spotlight intensity
        float theta = dot(L, normalize(-light.Direction));
        float epsilon = light.cutOff - light.outerCutOff;
        float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
        // float intensity = 1.0;
        // attenuation
        // float distance = length(light.position - fragPos);
        // float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));
        float attenuation = calculateAtten(Position,light.Position);
        vec3 radiance = light.Color * attenuation * intensity;
        finalColor += brdf * radiance * NdotL;
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