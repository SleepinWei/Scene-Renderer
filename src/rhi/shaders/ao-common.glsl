layout(set=0,binding=0,std140) uniform EffectsParameters {
    mat4 effectView;
    mat4 effectProjection;
    vec4 aoSettings; // world radius, world bias, power, enabled
    vec4 aoOptions; // horizon method, slices, steps per side, denoise
};
layout(set=1,binding=1) uniform sampler2D positionBuffer;
layout(set=1,binding=2) uniform sampler2D normalBuffer;
vec3 aoViewPosition(vec3 world) { return (effectView*vec4(world,1)).xyz; }
vec3 aoViewNormal(ivec2 pixel) {
    vec3 n=mat3(effectView)*texelFetch(normalBuffer,pixel,0).xyz;
    return dot(n,n)>1e-12?normalize(n):vec3(0,0,1);
}
// Shorter one-sided differences avoid crossing depth discontinuities.
// Geometry normals keep normal-map detail from self-occluding a flat plane.
vec3 aoGeometryNormal(ivec2 pixel,vec3 p) {
    ivec2 size=textureSize(positionBuffer,0);vec3 derivatives[2];
    for(int axis=0;axis<2;++axis) {
        ivec2 step=axis==0?ivec2(1,0):ivec2(0,1);
        vec4 left=texelFetch(positionBuffer,clamp(pixel-step,ivec2(0),size-1),0);
        vec4 right=texelFetch(positionBuffer,clamp(pixel+step,ivec2(0),size-1),0);
        vec3 a=p-aoViewPosition(left.xyz),b=aoViewPosition(right.xyz)-p;
        bool hasA=left.w>0. && dot(a,a)>1e-12,hasB=right.w>0. && dot(b,b)>1e-12;
        derivatives[axis]=hasA&&(!hasB||dot(a,a)<dot(b,b))?a:hasB?b:vec3(0);
    }
    vec3 n=cross(derivatives[0],derivatives[1]);
    if(dot(n,n)<1e-16)return aoViewNormal(pixel);
    n=normalize(n);return dot(n,-p)<0.?-n:n;
}
