#include "shoreline-coverage.glsl"
#include "water-wet.glsl"
bool hasShoreline(){return (materialFeatures.x & 2)!=0;}
vec2 beachUV(){return worldPosition.xz*vec2(1,-1)/shoreSurface.x;}
float beachWeight(){return hasShoreline()?shorelineCoverage(worldPosition.y-shoreHeight.x,normalize(worldNormal).y,texture(aoMap,uv).r,shoreHeight.y,shoreSurface.y,shoreSurface.z):0.;}
float rainCell(ivec2 at){
    at=clamp(at,ivec2(0),ivec2(15));int cell=at.y*16+at.x,word=cell/4;
    return float((rainWetCells[word/4][word%4]>>uint((cell%4)*8))&255u)/255.;
}
float rainGroundWetness(){
    if((materialFeatures.x & 8)!=0){vec2 p=clamp(uv,vec2(0),vec2(1))*16.-.5;ivec2 a=ivec2(floor(p));vec2 f=fract(p);
        return mix(mix(rainCell(a),rainCell(a+ivec2(1,0)),f.x),mix(rainCell(a+ivec2(0,1)),rainCell(a+ivec2(1)),f.x),f.y);}
    return (materialFeatures.x & 4)!=0?float(materialFeatures.y)/65535.:0.;
}
float combinedGroundWetness(){return max(groundWetness(),rainGroundWetness());}
float beachWetness(){vec4 h=waterGroundHistory();return max(rainGroundWetness(),mix(1.-smoothstep(-shoreHeight.z,shoreHeight.w,worldPosition.y-shoreHeight.x),h.y,h.w));}
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
        vec3 linearBase=pow(max(base.rgb,vec3(0)),vec3(2.2))*mix(1.,.45,combinedGroundWetness());
        base.rgb=pow(mix(linearBase,sand,beachWeight()),vec3(1./2.2));
    }
    // Apply wetness once to each blended material, including terrain outside
    // the beach mask. max combines coastal and rain wetness without double darkening.
    if(!hasShoreline())base.rgb*=pow(mix(1.,.45,combinedGroundWetness()),1./2.2);
    base.rgb=mix(base.rgb,vec3(.9),clamp(groundFoam(),0.,1.));
    return base;
}
float mappedMetallic(){return hasShoreline()?0.:clamp(sampleMaterial(metallicMap,uv).b*factors.x,0.,1.);}
float mappedRoughness(){
    if(!hasShoreline()){
        float r=clamp(sampleMaterial(roughnessMap,uv).g*factors.y*mix(1.,.5,combinedGroundWetness()),.045,1.);
        if((materialFeatures.x&16)!=0){
            // Mean normal length stores unresolved directional variation. Add
            // that variance to GGX slope variance instead of sharpening it
            // back into a sparkling full-strength normal at a distant mip.
            float lengthN=clamp(length(sampleMaterial(normalMap,uv).xyz*2.-1.),0.,1.);
            float variance=.5*(1.-lengthN)/max(lengthN,.25)*emissiveNormal.w*emissiveNormal.w;
            r=pow(clamp(r*r*r*r+variance,0.,1.),.25);
        }return r;
    }
    float sand=beachSample(roughnessMap,beachUV()).g;
    sand=mix(sand,max(.28,sand*.5),beachWetness());
    return clamp(mix(factors.y*mix(1.,.5,combinedGroundWetness()),sand,beachWeight()),.045,1.);
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
