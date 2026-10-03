#version 450
layout(location=2) in vec2 uv;layout(location=0) out vec4 depth;
layout(set=1,binding=0,std140) uniform MaterialData {vec4 albedoAlpha;vec4 factors;vec4 emissiveNormal;};layout(set=1,binding=1) uniform sampler2D albedoMap;
void main(){if(texture(albedoMap,uv).a*albedoAlpha.a<factors.w)discard;depth=vec4(gl_FragCoord.z,0,0,1);}
