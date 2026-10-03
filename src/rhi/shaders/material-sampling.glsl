layout(set=1,binding=6,std140) uniform MaterialExtension {vec4 materialLobes;vec4 materialSettings;ivec4 materialFeatures;vec4 shoreHeight;vec4 shoreSurface;};
layout(set=1,binding=7) uniform sampler2D specialMap;
#include "virtual-texture.glsl"
// Feature bit 0 is set by GpuMaterial when the five maps are physical VT atlases.
bool isVirtualMaterial(){return (materialFeatures.x & 1)!=0;}
vec4 sampleMaterial(sampler2D map,vec2 at){
    if(!isVirtualMaterial())return texture(map,at);
    ivec2 size=textureSize(specialMap,0);vec4 meta=texelFetch(specialMap,ivec2(0,size.y-2),0),mode=texelFetch(specialMap,ivec2(0,size.y-1),0);
    vec2 dx=dFdx(at*meta.x),dy=dFdy(at*meta.x);
    float level=clamp(log2(max(max(length(dx),length(dy)),1.)),0.,mode.x);
    return mix(virtualSampleLevel(map,specialMap,at,int(floor(level))),virtualSampleLevel(map,specialMap,at,int(ceil(level))),fract(level));
}
