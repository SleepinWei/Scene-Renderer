// Caller declares ddgiField[] and DdgiParameters. Irradiance is E/pi.
const int DDGI_STRIDE=321;
vec3 ddgiOctDecode(vec2 p){vec3 n=vec3(p,1.-abs(p.x)-abs(p.y));if(n.z<0.)n.xy=(1.-abs(n.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(n.xy,vec2(0)));return normalize(n);}
vec2 ddgiOctEncode(vec3 n){n/=max(abs(n.x)+abs(n.y)+abs(n.z),1e-8);if(n.z<0.)n.xy=(1.-abs(n.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(n.xy,vec2(0)));return n.xy*.5+.5;}
ivec2 ddgiWrap(ivec2 p,int n){if(p.x<0){p.x=-p.x-1;p.y=n-1-p.y;}if(p.x>=n){p.x=2*n-1-p.x;p.y=n-1-p.y;}if(p.y<0){p.y=-p.y-1;p.x=n-1-p.x;}if(p.y>=n){p.y=2*n-1-p.y;p.x=n-1-p.x;}return clamp(p,ivec2(0),ivec2(n-1));}
vec4 ddgiDirectional(int probe,vec3 direction,int channel){int n=channel==0?8:16,offset=channel==0?1:65;vec2 at=ddgiOctEncode(direction)*float(n)-.5;ivec2 lo=ivec2(floor(at));vec2 f=fract(at);vec4 v=vec4(0);for(int i=0;i<4;i++){ivec2 delta=ivec2(i%2,i/2),p=ddgiWrap(lo+delta,n);vec2 w=mix(1.-f,f,vec2(delta));v+=ddgiField[probe*DDGI_STRIDE+offset+p.y*n+p.x]*w.x*w.y;}return v;}
vec4 ddgiQuery(vec3 position,vec3 normal,vec3 viewDirection){
    vec3 grid=(position-ddgiOrigin.xyz)/ddgiOrigin.w;
    if(any(lessThan(grid,vec3(0)))||any(greaterThan(grid,vec3(ddgiCounts.xyz-1))))return vec4(0);
    vec3 biased=position+normal*ddgiBias.x+viewDirection*ddgiBias.y;
    vec3 cell=clamp((biased-ddgiOrigin.xyz)/ddgiOrigin.w,vec3(0),vec3(ddgiCounts.xyz-1));
    ivec3 base=min(ivec3(floor(cell)),ddgiCounts.xyz-2);vec3 f=clamp(cell-vec3(base),vec3(0),vec3(1));vec3 sum=vec3(0);float weightSum=0.,coverage=0.;
    for(int i=0;i<8;i++){
        ivec3 d=ivec3(i&1,(i>>1)&1,(i>>2)&1),p=base+d;
        int index=p.x+ddgiCounts.x*(p.y+ddgiCounts.y*p.z);
        vec4 state=ddgiField[index*DDGI_STRIDE];
        vec3 trilinear=mix(1.-f,f,vec3(d));float tri=trilinear.x*trilinear.y*trilinear.z;
        // Classified solid probes still count as initialized coverage. Falling
        // back to unoccluded ambient for them would reintroduce sky under floors.
        if(state.y>0.)coverage+=tri;
        if(state.x<.5)continue;
        vec3 probePosition=ddgiOrigin.xyz+vec3(p)*ddgiOrigin.w,delta=biased-probePosition;float distance=length(delta);
        vec3 direction=distance>1e-5?delta/distance:normal;
        float visibility=1.;if(ddgiBias.z>.5){vec2 moments=ddgiDirectional(index,direction,1).xy;float variance=max(moments.y-moments.x*moments.x,1e-4);float excess=max(distance-moments.x,0.);visibility=pow(variance/(variance+excess*excess),ddgiTemporal.y);}
        // Smooth wrap weight avoids discontinuous normal-side rejection.
        float facing=pow(max(.05,(dot(-direction,normal)+1.)*.5),2.);
        float w=tri*facing*visibility; if(w<.02)w*=w*w/(.02*.02);
        sum+=ddgiDirectional(index,normal,0).rgb*w;weightSum+=w;
    }
    // Do not fall back to unoccluded sky in valid cells behind a wall. Coverage
    // only reflects uninitialized probes; inactive probes never inject fallback sky.
    return vec4(weightSum>1e-9?sum/weightSum:vec3(0),clamp(coverage,0.,1.));
}
