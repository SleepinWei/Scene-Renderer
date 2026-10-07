// Cascades blend irradiance ratios rather than adding light twice. The finest
// valid receiver wins away from its border; larger cascades fill its edge/misses.
vec3 waterCausticFactor(vec3 point,sampler2D map,vec4 nearBounds,vec4 middleBounds,vec4 farBounds,vec4 projection,vec4 controls){
    if(controls.x<=0.)return vec3(1);
    vec2 coordinate=point.xz-projection.xy*(point.y-projection.z);vec3 gain=vec3(1);
    int count=controls.z>1.5?3:1;
    for(int cascade=count-1;cascade>=0;--cascade){
        vec4 bounds=cascade==0?nearBounds:cascade==1?middleBounds:farBounds;
        vec2 uv=(coordinate-bounds.xy)/max(bounds.z,.001);
        if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))continue;
        vec2 safe=clamp(uv,vec2(.5/512.),vec2(1.-.5/512.));
        vec4 value=textureLod(map,vec2((float(cascade)+safe.x)/3.,safe.y),0);
        float match=1.-smoothstep(max(.06,bounds.w*.5),max(.2,bounds.w*2.),abs((point.y-projection.z)-value.w));
        float edge=min(min(uv.x,uv.y),min(1.-uv.x,1.-uv.y));
        gain=mix(gain,max(value.rgb,vec3(0)),match*smoothstep(0.,.12,edge));
    }
    return mix(vec3(1),gain,clamp(controls.x,0.,1.));
}
