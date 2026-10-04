#ifndef VIRTUAL_TEXTURE_GLSL
#define VIRTUAL_TEXTURE_GLSL
// Page table stacks square mip grids vertically, followed by two metadata rows.
vec4 virtualSampleLevel(sampler2D atlas,sampler2D table,vec2 uv,int requested) {
    ivec2 tableSize=textureSize(table,0);
    vec4 meta=texelFetch(table,ivec2(0,tableSize.y-2),0),mode=texelFetch(table,ivec2(0,tableSize.y-1),0);
    int maxMip=int(mode.x),level=clamp(requested,0,maxMip),offset=0;
    for(int m=0;m<level;m++)offset+=tableSize.x>>m;
    vec4 result=vec4(0);float remaining=1.;
    for(int m=level;m<=maxMip;m++) {
        float dimension=meta.x/exp2(float(m));
        vec2 p=mode.y>.5?clamp(uv,0.,1.)*(dimension-1.):clamp(uv,0.,1.)*dimension-.5;
        int count=tableSize.x>>m;
        ivec2 tile=clamp(ivec2(floor(p/meta.y)),ivec2(0),ivec2(count-1));
        vec4 entry=texelFetch(table,ivec2(tile.x,offset+tile.y),0);
        if(entry.w>.5) {
            // Aprons join equal-resolution pages. A missing neighbor also needs
            // a continuous transition to its parent, including diagonal corners.
            // Distance to each missing page's box is identical on either side of
            // a shared edge; both resident pages therefore choose the same blend.
            float coverage=1.,band=meta.w*4.;
            if(m<maxMip)for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++) {
                ivec2 neighbor=tile+ivec2(x,y);
                if(any(lessThan(neighbor,ivec2(0))) || any(greaterThanEqual(neighbor,ivec2(count))))continue;
                vec2 low=vec2(neighbor)*meta.y;
                float distanceToPage=length(p-clamp(p,low,low+meta.y));
                if(distanceToPage<band && texelFetch(table,ivec2(neighbor.x,offset+neighbor.y),0).w<.5)
                    coverage=min(coverage,smoothstep(0.,band,distanceToPage));
            }
            if(coverage>0.) {
                float pitch=meta.y+2.*meta.w;
                vec2 texel=entry.xy*pitch+meta.w+p-vec2(tile)*meta.y;
                vec4 value;
                if(mode.y>.5){
                    ivec2 base=ivec2(floor(texel));vec2 weight=fract(texel);
                    value=mix(mix(texelFetch(atlas,base,0),texelFetch(atlas,base+ivec2(1,0),0),weight.x),mix(texelFetch(atlas,base+ivec2(0,1),0),texelFetch(atlas,base+ivec2(1,1),0),weight.x),weight.y);
                }else value=textureLod(atlas,(texel+.5)/(meta.z*pitch),0.);
                result+=remaining*coverage*value;remaining*=1.-coverage;
                if(remaining<1e-6)return result;
            }
        }
        offset+=tableSize.x>>m;
    }
    return result;
}
#endif
