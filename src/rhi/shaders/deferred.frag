#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=1,binding=1) uniform sampler2D positionBuffer;
layout(set=1,binding=2) uniform sampler2D normalBuffer;
layout(set=1,binding=3) uniform sampler2D albedoBuffer;
layout(set=1,binding=4) uniform sampler2D emissiveBuffer;
layout(set=2,binding=0) uniform sampler2D materialEffectsBuffer;
layout(set=2,binding=1) uniform sampler2D tangentBuffer;
#include "pbr-lighting.glsl"
void main() {
    vec4 position = texture(positionBuffer,uv);
    if (position.w == 0) { color = vec4(0,0,0,1);return; }
    vec4 normal = texture(normalBuffer,uv), albedo = texture(albedoBuffer,uv), emissive = texture(emissiveBuffer,uv);
    color = vec4(shadePbrExtended(position.xyz,normalize(normal.xyz),normalize(texture(tangentBuffer,uv).xyz),albedo.xyz,albedo.w,normal.w,emissive.w,emissive.xyz,texture(materialEffectsBuffer,uv),0.),1);
}
