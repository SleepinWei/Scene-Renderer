#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(location=1) out vec4 motion;
#include "cloud-parameters.glsl"
#include "cloud-intersection.glsl"
layout(set=1,binding=0) uniform sampler2D cloudResolved;
layout(set=1,binding=1) uniform sampler2D cloudResolvedDepth;
layout(set=1,binding=2) uniform sampler2D sceneDepth;
void main(){
    float surface=cloudSurface(uv,texture(sceneDepth,uv).r);vec2 interval=cloudInterval(cloudRay(uv),surface);
    if(interval.y<=interval.x)discard;
    vec2 p=uv*vec2(cloudGrid.xy)-.5;ivec2 base=ivec2(floor(p));vec2 f=fract(p);
    vec4 cloud=vec4(0);float sum=0.;
    for(int y=0;y<2;y++)for(int x=0;x<2;x++) {
        ivec2 at=clamp(base+ivec2(x,y),ivec2(0),cloudGrid.xy-1);vec4 meta=texelFetch(cloudResolvedDepth,at,0);
        float w=(x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y);
        w*=exp(-abs(meta.y-surface)/max(20.,surface*.02));
        if(meta.x>surface)w=0.;cloud+=texelFetch(cloudResolved,at,0)*w;sum+=w;
    }
    if(sum<1e-6)discard;cloud/=sum;
    float opacity=clamp(1.-cloud.a,0.,1.);if(opacity<1e-5)discard;
    color=vec4(min(cloud.rgb/opacity,vec3(65000)),opacity);
    // Clouds own wind-compensated temporal resolve; reject the generic sky
    // history here, which would reproject them as an infinitely distant sky.
    motion=vec4(0,0,0,-1);
}
