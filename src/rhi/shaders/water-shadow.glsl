// Volume vertices have no receiver plane. Use a small fixed PCF footprint
// rather than the surface shader's normal bias and PCSS penumbra estimate.
#define SHADOW_SET 2
#define SHADOW_BINDING 3
#include "shadow-data.glsl"
layout(set=2,binding=4) uniform sampler2D waterShadowAtlas;
float waterSunVisibility(vec3 point,int light) {
    if(light<0||shadowSettings.y<=0)return 1.;
    ivec4 info=shadowLights[light];if(info.y<=0||info.z!=0)return 1.;
    float depth=-(shadowCameraView*vec4(point,1)).z;vec4 splits=shadowSplits[light];
    int cascade=depth>splits.w?4:depth>splits.z?3:depth>splits.y?2:depth>splits.x?1:0;
    int tile=info.x+min(cascade,info.y-1);vec4 clip=shadowMatrices[tile]*vec4(point,1);
    if(clip.w<=0)return 1.;vec3 q=clip.xyz/clip.w;
    if(q.z<0||q.z>1||any(greaterThan(abs(q.xy),vec2(1))))return 1.;
    vec4 rect=shadowRects[tile];ivec2 size=textureSize(waterShadowAtlas,0);
    vec2 uv=(q.xy*vec2(.5,-.5)+.5)*rect.zw+rect.xy;
    vec2 xy=uv*vec2(size)-.5,f=fract(xy);ivec2 at=ivec2(floor(xy));
    ivec2 low=ivec2(round(rect.xy*vec2(size))),high=ivec2(round((rect.xy+rect.zw)*vec2(size)))-1;
    float taps[4];for(int i=0;i<4;++i)taps[i]=q.z-shadowSettings.x<=texelFetch(waterShadowAtlas,clamp(at+ivec2(i%2,i/2),low,high),0).r?1.:0.;
    return mix(mix(taps[0],taps[1],f.x),mix(taps[2],taps[3],f.x),f.y);
}
