// Shared patch interpretation for rendering and underwater clipping. State
// carries absolute bed height; FFT is replaced (not added twice) in shallow water.
vec4 coastSample(sampler2D state,vec2 xz,vec4 bounds) {
    return textureLod(state,(xz-bounds.xy)/max(bounds.z,.001),0);
}
float coastCellHeight(vec4 q,float level){return q.x+(q.x>.005?q.w:min(q.w,level));}
float coastElevation(sampler2D state,vec2 xz,vec4 bounds,float level) {
    ivec2 size=textureSize(state,0);vec2 pixel=(xz-bounds.xy)/max(bounds.z,.001)*vec2(size)-.5;
    ivec2 p=ivec2(floor(pixel));vec2 f=fract(pixel);ivec2 high=size-1;
    float a=coastCellHeight(texelFetch(state,clamp(p,ivec2(0),high),0),level);
    float b=coastCellHeight(texelFetch(state,clamp(p+ivec2(1,0),ivec2(0),high),0),level);
    float c=coastCellHeight(texelFetch(state,clamp(p+ivec2(0,1),ivec2(0),high),0),level);
    float d=coastCellHeight(texelFetch(state,clamp(p+ivec2(1,1),ivec2(0),high),0),level);
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
float coastBlend(vec2 xz,vec4 state,vec4 bounds,float level) {
    if(coastBedInfo.x<.5)return 0.;
    vec2 bedUV=(coastInverse*vec4(xz.x,0,xz.y,1)).xz*.5+.5;
    if(any(lessThan(bedUV,vec2(0)))||any(greaterThan(bedUV,vec2(1))))return 0.;
    vec2 cell=(xz-bounds.xy)/max(bounds.w,.001),cells=vec2(bounds.z/max(bounds.w,.001));
    float edge=min(min(cell.x,cell.y),min(cells.x-cell.x,cells.y-cell.y));
    return smoothstep(0.,8.,edge)*(1.-smoothstep(2.,6.,max(level-state.w,0.)));
}
float coastHeight(vec2 xz,float fftHeight,sampler2D state,vec4 bounds,float level,float enabled) {
    if(enabled<.5)return fftHeight;
    vec4 q=coastSample(state,xz,bounds);return mix(fftHeight,coastElevation(state,xz,bounds,level),coastBlend(xz,q,bounds,level));
}
float coastDetailWeight(vec2 xz,sampler2D state,vec4 bounds,float level,float enabled) {
    if(enabled<.5)return 1.;
    vec4 q=coastSample(state,xz,bounds);
    return mix(1.,smoothstep(.01,.4,q.x),coastBlend(xz,q,bounds,level));
}
float coastDetailedHeight(vec2 xz,float fullHeight,float macroHeight,sampler2D state,vec4 bounds,float level,float enabled) {
    return coastHeight(xz,macroHeight,state,bounds,level,enabled)+(fullHeight-macroHeight)*coastDetailWeight(xz,state,bounds,level,enabled);
}
