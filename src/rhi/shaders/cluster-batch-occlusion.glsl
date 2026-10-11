// Same-frame standard-Z max-depth pyramid. Fail open at near/eye crossings,
// non-finite projections, and uncovered/alpha pixels (clear depth is one).
bool unoccluded(ClusterMesh mesh,GeometryCluster c){
    vec2 low=vec2(1e30),high=vec2(-1e30);float nearest=1.0;
    if(batchStage.y!=0u){
        vec3 center=(c.low.xyz+c.high.xyz)*.5,extent=(c.high.xyz-c.low.xyz)*.5;
        vec4 clip=mesh.vp*vec4(center,1.0);
        vec4 radius=abs(mesh.vp[0])*extent.x+abs(mesh.vp[1])*extent.y+abs(mesh.vp[2])*extent.z+mesh.rounding;
        if(any(isnan(clip))||any(isinf(clip))||any(isnan(radius))||any(isinf(radius))||clip.w-radius.w<=1e-6||clip.z-radius.z<=1e-6)return true;
        vec2 a=(clip.xy-radius.xy)/(clip.w-radius.w),b=(clip.xy-radius.xy)/(clip.w+radius.w);
        vec2 d=(clip.xy+radius.xy)/(clip.w-radius.w),e=(clip.xy+radius.xy)/(clip.w+radius.w);
        low=min(min(a,b),min(d,e));high=max(max(a,b),max(d,e));
        // Z and W are tightly correlated in a perspective projection. Keep
        // their exact corner extrema rather than dividing independent ranges,
        // which would destroy useful occlusion while remaining conservative.
        vec4 zRow=vec4(mesh.vp[0].z,mesh.vp[1].z,mesh.vp[2].z,mesh.vp[3].z);
        vec4 wRow=vec4(mesh.vp[0].w,mesh.vp[1].w,mesh.vp[2].w,mesh.vp[3].w);
        for(uint corner=0u;corner<8u;++corner){
            vec4 p=vec4((corner&1u)!=0u?c.high.x:c.low.x,(corner&2u)!=0u?c.high.y:c.low.y,(corner&4u)!=0u?c.high.z:c.low.z,1.0);
            nearest=min(nearest,(dot(zRow,p)-mesh.rounding.z)/(dot(wRow,p)+mesh.rounding.w));
        }
    }else for(uint corner=0u;corner<8u;++corner){
        vec3 p=vec3((corner&1u)!=0u?c.high.x:c.low.x,(corner&2u)!=0u?c.high.y:c.low.y,(corner&4u)!=0u?c.high.z:c.low.z);
        vec4 clip=mesh.vp*vec4(p,1.0),error=mesh.rounding;
        if(any(isnan(clip))||any(isinf(clip))||clip.w<=error.w+1e-6||clip.z<=error.z+1e-6)return true;
        // Both denominator signs are needed when numerator components change sign.
        vec2 a=(clip.xy-error.xy)/(clip.w-error.w),b=(clip.xy-error.xy)/(clip.w+error.w);
        vec2 d=(clip.xy+error.xy)/(clip.w-error.w),e=(clip.xy+error.xy)/(clip.w+error.w);
        low=min(low,min(min(a,b),min(d,e)));high=max(high,max(max(a,b),max(d,e)));
        nearest=min(nearest,(clip.z-error.z)/(clip.w+error.w));
    }
    if(high.x< -1.0||low.x>1.0||high.y< -1.0||low.y>1.0)return true;
    vec4 rect=vec4((low.x*.5+.5)*float(batchCounts.y)-2.0,(.5-high.y*.5)*float(batchCounts.z)-2.0,
                   (high.x*.5+.5)*float(batchCounts.y)+2.0,(.5-low.y*.5)*float(batchCounts.z)+2.0);
    rect=clamp(rect,vec4(0),vec4(float(batchCounts.y-1u),float(batchCounts.z-1u),float(batchCounts.y-1u),float(batchCounts.z-1u)));
    float size=max(rect.z-rect.x+1.0,rect.w-rect.y+1.0);
    int baseMip=0;ivec2 baseSize=textureSize(clusterDepth,0);
    while(baseMip<30&&any(notEqual(max(ivec2(batchCounts.yz)>>baseMip,ivec2(1)),baseSize)))++baseMip;
    int mip=clamp(int(floor(log2(max(size,1.0))))-baseMip,0,int(batchCounts.w)-1);ivec2 extent=textureSize(clusterDepth,mip);
    ivec2 first=clamp(ivec2(floor(rect.xy/exp2(float(mip+baseMip)))),ivec2(0),extent-1);
    ivec2 last=clamp(ivec2(floor(rect.zw/exp2(float(mip+baseMip)))),ivec2(0),extent-1);
    float farthest=0.0;
    for(int y=first.y;y<=last.y;++y)for(int x=first.x;x<=last.x;++x)farthest=max(farthest,texelFetch(clusterDepth,ivec2(x,y),mip).r);
    return !(nearest>farthest+1e-4);
}
