#ifndef VIRTUAL_TEXTURE_GLSL
#define VIRTUAL_TEXTURE_GLSL
// Page table stacks square mip grids vertically, followed by two metadata rows.
vec4 virtualSampleLevel(sampler2D atlas,sampler2D table,vec2 uv,int requested) {
    ivec2 tableSize=textureSize(table,0);
    vec4 meta=texelFetch(table,ivec2(0,tableSize.y-2),0),mode=texelFetch(table,ivec2(0,tableSize.y-1),0);
    int maxMip=int(mode.x),level=clamp(requested,0,maxMip),offset=0;
    for(int m=0;m<level;m++)offset+=tableSize.x>>m;
    vec2 p=vec2(0);ivec2 tile=ivec2(0);vec4 entry=vec4(0);
    for(int m=level;m<=maxMip;m++) {
        float dimension=meta.x/exp2(float(m));
        p=mode.y>.5?clamp(uv,0.,1.)*(dimension-1.):clamp(uv,0.,1.)*dimension-.5;
        tile=clamp(ivec2(floor(p/meta.y)),ivec2(0),ivec2((tableSize.x>>m)-1));
        entry=texelFetch(table,ivec2(tile.x,offset+tile.y),0);
        if(entry.w>.5)break;
        offset+=tableSize.x>>m;
    }
    float pitch=meta.y+2.*meta.w;
    vec2 texel=entry.xy*pitch+meta.w+p-vec2(tile)*meta.y;
    if(mode.y>.5){
        // Geometry and derivative samples need full float interpolation precision.
        // Hardware filter weights can be quantized, distorting shallow slopes.
        ivec2 base=ivec2(floor(texel));vec2 weight=fract(texel);
        return mix(mix(texelFetch(atlas,base,0),texelFetch(atlas,base+ivec2(1,0),0),weight.x),mix(texelFetch(atlas,base+ivec2(0,1),0),texelFetch(atlas,base+ivec2(1,1),0),weight.x),weight.y);
    }
    return textureLod(atlas,(texel+.5)/(meta.z*pitch),0.);
}
#endif
