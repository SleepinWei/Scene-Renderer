#include "shoreline-coverage.glsl"
bool hasShoreline(){return (materialFeatures.x & 2)!=0;}
vec2 beachUV(){return worldPosition.xz*vec2(1,-1)/shoreSurface.x;}
float beachWeight(){return hasShoreline()?shorelineCoverage(worldPosition.y-shoreHeight.x,normalize(worldNormal).y,texture(aoMap,uv).r,shoreHeight.y,shoreSurface.y,shoreSurface.z):0.;}
float beachWetness(){return 1.-smoothstep(-shoreHeight.z,shoreHeight.w,worldPosition.y-shoreHeight.x);}
// Mips occupy vertically stacked square regions. Four texel fetches wrap within
// each mip, so repeat filtering cannot bleed into an adjacent atlas region.
vec4 beachMip(sampler2D map,vec2 at,int level){
    int width=textureSize(map,0).x,n=max(width>>level,1),row=2*(width-n);
    vec2 p=fract(at)*float(n)-.5,weight=fract(p);ivec2 a=ivec2(floor(p));
    ivec2 lo=(a%n+n)%n,hi=(a+1)%n;
    return mix(mix(texelFetch(map,ivec2(lo.x,row+lo.y),0),texelFetch(map,ivec2(hi.x,row+lo.y),0),weight.x),
               mix(texelFetch(map,ivec2(lo.x,row+hi.y),0),texelFetch(map,ivec2(hi.x,row+hi.y),0),weight.x),weight.y);
}
vec4 beachSample(sampler2D map,vec2 at){
    float size=float(textureSize(map,0).x);
    float footprint=max(length(dFdx(at*size)),length(dFdy(at*size)));
    float level=clamp(log2(max(footprint,1.)),0.,log2(size));
    return mix(beachMip(map,at,int(floor(level))),beachMip(map,at,int(ceil(level))),fract(level));
}
vec4 mappedBase(){
    vec4 base=sampleMaterial(albedoMap,uv)*albedoAlpha;
    if(hasShoreline()) {
        vec3 sand=pow(max(beachSample(metallicMap,beachUV()).rgb,vec3(0)),vec3(2.2));
        sand*=mix(1.,.45,beachWetness());
        vec3 linearBase=pow(max(base.rgb,vec3(0)),vec3(2.2));
        base.rgb=pow(mix(linearBase,sand,beachWeight()),vec3(1./2.2));
    }
    return base;
}
float mappedMetallic(){return hasShoreline()?0.:clamp(sampleMaterial(metallicMap,uv).b*factors.x,0.,1.);}
float mappedRoughness(){
    if(!hasShoreline())return clamp(sampleMaterial(roughnessMap,uv).g*factors.y,.045,1.);
    float sand=beachSample(roughnessMap,beachUV()).g;
    sand=mix(sand,max(.28,sand*.5),beachWetness());
    return clamp(mix(factors.y,sand,beachWeight()),.045,1.);
}
float mappedAO(){return hasShoreline()?mix(1.,beachSample(roughnessMap,beachUV()).r,beachWeight()*factors.z):mix(1.,sampleMaterial(aoMap,uv).r,factors.z);}
vec3 beachNormal(vec3 N){
    vec3 tangent=vec3(1,0,0)-N*N.x;
    if(dot(tangent,tangent)<1e-8)return N;
    tangent=normalize(tangent);vec3 bitangent=cross(N,tangent);
    vec3 sampled=normalize(beachSample(normalMap,beachUV()).xyz*2.-1.);
    sampled.xy*=shoreSurface.w;
    return normalize(mix(N,normalize(mat3(tangent,bitangent,N)*sampled),beachWeight()));
}
