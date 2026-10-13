#version 450
#extension GL_GOOGLE_include_directive : require
// Reuse the native PBR vertex shader so projected positions and alpha-mask
// interpolation have the exact same compiled vertex stage as the beauty pass.
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 diagnosticKey;
layout(location=2) in vec2 uv;
layout(set=0,binding=2,std430) readonly buffer FlowColors {vec4 colors[];};
layout(set=1,binding=0,std140) uniform MaterialData {vec4 albedoAlpha;vec4 factors;vec4 emissiveNormal;};
layout(set=1,binding=1) uniform sampler2D albedoMap;
#include "material-sampling.glsl"
layout(location=0) out vec4 outputColor;
layout(location=1) out float outputKey;
void main(){
    if(sampleMaterial(albedoMap,uv).a*albedoAlpha.a<factors.w)discard;
    vec3 face=cross(dFdx(worldPosition),dFdy(worldPosition));
    float shade=dot(face,face)>1e-12?.55+.45*abs(dot(normalize(face),normalize(vec3(.4,1,.7)))):1.;
    // The native vertex stage normalizes the vector. Ratios retain both
    // 16-bit halves of the unique raster key; the +Z component is always 1.
    uvec2 halves=uvec2(round(clamp(diagnosticKey.xy/diagnosticKey.z,0.,1.)*65535.));
    uint key=halves.x|(halves.y<<16);
    outputKey=float(key);
    outputColor=vec4(colors[min(key,uint(colors.length()-1))].rgb*shade,1);
}
