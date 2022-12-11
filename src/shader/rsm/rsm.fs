#version 430 core

out vec4 FragColor;
in vec2 TexCoords;

in vec3 Normal;
in vec3 FragPos;
in vec4 FragPosLightSpace;

uniform sampler2D depthMap;
uniform sampler2D normalMap;
uniform sampler2D worldPosMap;
uniform sampler2D fluxMap;
uniform sampler2D randomMap;

uniform int sample_num;
uniform float sample_radius;


uniform sampler2D inTexture;
uniform sampler2D outTexture;

vec3 shading()
{
	vec3 projCoords=FragPosLightSpace.xyz/FragPosLightSpace.w;
	projCoords=projCoords*0.5+0.5;
    vec3 indirect=vec3(0.0,0.0,0.0);
    for (int i=0; i<sample_num; i=i+1){
        vec3 r=texelFetch(randomMap, ivec2(i, 0), 0).xyz;
        vec2 sample_coord=projCoords.xy+r.xy*sample_radius;
        float weight=r.z;

        vec3 target_normal=normalize(texture(normalMap, sample_coord).xyz);
        vec3 target_worldPos=texture(worldPosMap, sample_coord).xyz;
        vec3 target_flux=texture(fluxMap, sample_coord).rgb;

        vec3 indirect_result=target_flux*max(0, dot(target_normal, FragPos-target_worldPos)) \
            *max(0, dot(Normal, target_worldPos-FragPos))/pow(length(FragPos-target_worldPos),4.0);
        indirect_result*=weight;
        indirect+=indirect_result;
    }
    indirect=clamp(indirect/sample_num, 0.0, 1.0);

    return indirect * 20;
}


void main(){
    vec3 color = shading();

    // color correction
    float gamma = 2.2;
	FragColor = vec4(color,1.0);
	FragColor.rgb = pow(FragColor.rgb, vec3(1.0/gamma));

    FragColor = FragColor + texture(inTexture,TexCoords);
}