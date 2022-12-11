#version 430 core

out vec4 FragColor;
in vec2 texCoords;

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
const float RSM_INTENSITY = 0.4;

uniform sampler2D inTexture;

vec3 shading()
{
	vec3 projCoords=FragPosLightSpace.xyz/FragPosLightSpace.w;
	projCoords=projCoords*0.5+0.5;
	//计算间接光照
	vec3 indirect=vec3(0.0,0.0,0.0);
	for (int i=0; i<sample_num; i=i+1){
		vec3 r=texelFetch(randomMap, ivec2(i, 0), 0).xyz;
		vec2 sample_coord=projCoords.xy+r.xy*sample_radius;
		float weight=r.z;

		vec3 target_normal=texture(normalMap, sample_coord).xyz;
		vec3 target_worldPos=texture(worldPosMap, sample_coord).xyz;
		vec3 target_flux=texture(fluxMap, sample_coord).rgb;

        vec3 dis=FragPos-target_worldPos;
		vec3 indirect_result=target_flux*max(0, dot(target_normal, dis))*max(0, dot(Normal, -dis));
        indirect_result *=  weight/max(1.0,pow(length(dis),4.0));
		indirect+=indirect_result;
	}
	indirect=clamp(indirect/sample_num, 0.0, 1.0);

    return indirect * RSM_INTENSITY;
}

void main(){
    vec3 color = shading();

    color = color + texture(inTexture,gl_FragCoord.xy/vec2(1600,900)).rgb;
	FragColor = vec4(color,1.0);
}