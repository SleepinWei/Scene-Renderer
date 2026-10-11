#include "ao-parameters.glsl"
layout(set=1,binding=1) uniform sampler2D positionBuffer;
layout(set=1,binding=2) uniform sampler2D normalBuffer;
layout(set=1,binding=4) uniform sampler2D aoLinearDepth;
// Vulkan/Metal RHI NDC depth is 0..1; row zero is the top of the image.
vec3 aoReconstructPosition(ivec2 pixel,float depth) {
    vec2 ndc=(vec2(pixel)+.5)/vec2(textureSize(aoLinearDepth,0))*vec2(2,-2)+vec2(-1,1);
    if(aoDepthOptions.w>.5) {
        float clipW=abs(effectProjection[3][3])>.5?1.:depth;
        vec2 xy=(ndc*clipW+effectProjection[2].xy*depth-effectProjection[3].xy)/vec2(effectProjection[0][0],effectProjection[1][1]);
        return vec3(xy,-depth);
    }
    vec4 a=aoInverseProjection*vec4(ndc,0,1),b=aoInverseProjection[2];
    // Interpolate homogeneous endpoints at z=-depth before dividing by w.
    // Handles perspective, asymmetric/jittered frusta and orthographic cameras.
    float t=(-depth*a.w-a.z)/(b.z+depth*b.w);
    vec4 point=a+b*t;return point.xyz/point.w;
}
vec3 aoViewPosition(vec3 world) { return (effectView*vec4(world,1)).xyz; }
vec3 aoViewNormal(ivec2 pixel) {
    vec3 n=mat3(effectView)*texelFetch(normalBuffer,pixel,0).xyz;
    return dot(n,n)>1e-12?normalize(n):vec3(0,0,1);
}
vec4 aoPosition(ivec2 pixel) {
    if(aoDepthOptions.x>.5) {
        float depth=texelFetch(aoLinearDepth,pixel,0).r;
        return depth>0.?vec4(aoReconstructPosition(pixel,depth),1):vec4(0);
    }
    vec4 world=texelFetch(positionBuffer,pixel,0);
    return vec4(aoViewPosition(world.xyz),world.w);
}
// Reject only blocks proven empty or entirely behind the radius. Never use
// coarse min depth as an occluder: surviving taps keep full-resolution geometry.
bool aoRejectBlock(ivec2 pixel,float offsetPixels,float centerDepth) {
    if(aoDepthOptions.x<.5 || aoDepthOptions.y<.5 || offsetPixels<4.)return false;
    int mip=clamp(int(floor(log2(offsetPixels*.5))),1,int(aoDepthOptions.z)-1);
    ivec2 size=textureSize(aoLinearDepth,mip);
    float nearest=texelFetch(aoLinearDepth,min(pixel>>mip,size-1),mip).r;
    return nearest==0. || nearest>centerDepth+aoSettings.x+max(1e-5,centerDepth*1e-6);
}
// Shorter one-sided differences avoid crossing depth discontinuities.
// Geometry normals keep normal-map detail from self-occluding a flat plane.
vec3 aoGeometryNormal(ivec2 pixel,vec3 p) {
    ivec2 size=textureSize(positionBuffer,0);vec3 derivatives[2];
    for(int axis=0;axis<2;++axis) {
        ivec2 step=axis==0?ivec2(1,0):ivec2(0,1);
        vec4 left=aoPosition(clamp(pixel-step,ivec2(0),size-1));
        vec4 right=aoPosition(clamp(pixel+step,ivec2(0),size-1));
        vec3 a=p-left.xyz,b=right.xyz-p;
        bool hasA=left.w>0. && dot(a,a)>1e-12,hasB=right.w>0. && dot(b,b)>1e-12;
        derivatives[axis]=hasA&&(!hasB||dot(a,a)<dot(b,b))?a:hasB?b:vec3(0);
    }
    vec3 n=cross(derivatives[0],derivatives[1]);
    if(dot(n,n)<1e-16)return aoViewNormal(pixel);
    n=normalize(n);return dot(n,-p)<0.?-n:n;
}
