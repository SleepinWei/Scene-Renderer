// Projection coordinates follow the mean refracted solar ray. Unlike an XZ
// height map, this captures lit sloping/vertical rock faces for oblique sun.
vec2 causticSourceCoordinate(vec3 point){return point.xz-receiverProjection.xy*(point.y-receiverProjection.z);}
ivec2 causticReceiverPixel(vec2 uv){return ivec2(clamp(uv*float(grid.y),vec2(0),vec2(grid.y-1)))+ivec2(grid.z*grid.y,0);}
bool causticReceiverHeight(vec3 point,out float height){
    if(receiverProjection.w>.5){
        vec2 uv=(causticSourceCoordinate(point)-photonPatch.xy)/photonPatch.z;
        if(all(greaterThanEqual(uv,vec2(0)))&&all(lessThanEqual(uv,vec2(1)))){
            vec2 f=uv*float(grid.y)-.5;ivec2 lo=ivec2(floor(f));vec2 weight=fract(f);
            ivec2 offset=ivec2(grid.z*grid.y,0),last=ivec2(grid.y-1);
            vec4 a=texelFetch(causticReceiverPosition,clamp(lo,ivec2(0),last)+offset,0),b=texelFetch(causticReceiverPosition,clamp(lo+ivec2(1,0),ivec2(0),last)+offset,0);
            vec4 c=texelFetch(causticReceiverPosition,clamp(lo+ivec2(0,1),ivec2(0),last)+offset,0),d=texelFetch(causticReceiverPosition,clamp(lo+ivec2(1,1),ivec2(0),last)+offset,0);
            // Do not interpolate a rock silhouette into the floor below it.
            float spread=max(max(a.y,b.y),max(c.y,d.y))-min(min(a.y,b.y),min(c.y,d.y));
            if(min(min(a.w,b.w),min(c.w,d.w))>.5&&spread<.75){height=mix(mix(a.y,b.y,weight.x),mix(c.y,d.y,weight.x),weight.y);return true;}
            vec4 nearest=texelFetch(causticReceiverPosition,causticReceiverPixel(uv),0);if(nearest.w>.5){height=nearest.y;return true;}
        }
    }
    return causticBed(point.xz,height);
}
