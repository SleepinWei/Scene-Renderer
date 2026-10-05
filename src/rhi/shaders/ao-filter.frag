#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
#include "ao-common.glsl"
layout(set=1,binding=3) uniform sampler2D aoRawBuffer;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy),size=textureSize(positionBuffer,0);
    vec4 world=texelFetch(positionBuffer,pixel,0);
    if(aoSettings.w==0. || world.w==0. || aoSettings.x<=0.) {color=vec4(1);return;}
    float value=texelFetch(aoRawBuffer,pixel,0).r;
    if(aoOptions.w>.5) {
        vec3 p=aoViewPosition(world.xyz),N=aoGeometryNormal(pixel,p),shadingN=aoViewNormal(pixel);
        float sum=0.,weight=0.,sigma=max(.015*aoSettings.x,.002);
        for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x) {
            ivec2 at=pixel+ivec2(x,y);
            if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,size)))continue;
            vec4 neighbor=texelFetch(positionBuffer,at,0);if(neighbor.w==0.)continue;
            vec3 delta=aoViewPosition(neighbor.xyz)-p;
            if(dot(delta,delta)>aoSettings.x*aoSettings.x)continue;
            float w=exp(-float(x*x+y*y)/4.-abs(dot(delta,N))/sigma)*pow(max(dot(shadingN,aoViewNormal(at)),0.),16.);
            sum+=texelFetch(aoRawBuffer,at,0).r*w;weight+=w;
        }
        value=sum/max(weight,1e-8);
    }
    value=pow(clamp(value,0.,1.),aoSettings.z);color=vec4(value,value,value,1);
}
