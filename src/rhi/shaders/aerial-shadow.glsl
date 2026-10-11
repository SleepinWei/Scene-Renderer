// Volume samples have no surface normal or receiver-plane derivative. Four
// depth comparisons give soft visibility without applying a surface offset.
float aerialShadowTile(int tile,vec3 p){
    vec4 clip=shadowMatrices[tile]*vec4(p,1);vec3 ndc=clip.xyz/clip.w;
    vec2 uv=ndc.xy*vec2(.5,-.5)+.5;
    if(ndc.z<0.||ndc.z>1.||any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))return 1.;
    vec4 rect=shadowRects[tile];ivec2 size=textureSize(aerialShadowAtlas,0);
    vec2 at=(rect.xy+uv*rect.zw)*vec2(size)-.5,f=fract(at);ivec2 b=ivec2(floor(at));
    ivec2 lo=ivec2(round(rect.xy*vec2(size))),hi=lo+ivec2(round(rect.zw*vec2(size)))-1;
    float taps[4];for(int i=0;i<4;++i)taps[i]=ndc.z-shadowSettings.x<=texelFetch(aerialShadowAtlas,clamp(b+ivec2(i%2,i/2),lo,hi),0).r?1.:0.;
    return mix(mix(taps[0],taps[1],f.x),mix(taps[2],taps[3],f.x),f.y);
}
float aerialSunVisibility(vec3 p){
    int light=int(aerialVisibility.x);if(aerialVisibility.y<.5||light<0)return 1.;
    float visibility=1.;float depth=-(shadowCameraView*vec4(p,1)).z;
    if(shadowSettings.y>0.&&shadowLights[light].y==5&&depth>=0.&&depth<shadowCascades.y){
        vec4 s=shadowSplits[light];int cascade=depth>s.w?4:depth>s.z?3:depth>s.y?2:depth>s.x?1:0;
        int tile=shadowLights[light].x+cascade;visibility=aerialShadowTile(tile,p);
        if(cascade<4&&shadowCascades.z>0.){
            float end=s[cascade],start=cascade==0?shadowCascades.x:s[cascade-1];
            float blend=smoothstep(end-(end-start)*shadowCascades.z,end,depth);
            if(blend>0.)visibility=mix(visibility,aerialShadowTile(tile+1,p),blend);
        }
        float fade=mix(s.w,shadowCascades.y,1.-shadowCascades.w);
        visibility=mix(visibility,1.,smoothstep(fade,shadowCascades.y,depth));
    }
    // The cloud field retains raw Beer transmission. Ground strength is an
    // independent artistic control and does not brighten shadowed air.
    if(cloudShadowProjection.w!=0.&&p.y<cloudShadowProjection.w){
        vec2 uv=(p.xz-cloudShadowProjection.xy*(p.y-cloudShadowProjection.z)-cloudShadowPatch.xy)/cloudShadowPatch.zw;
        if(all(greaterThanEqual(uv,vec2(0)))&&all(lessThanEqual(uv,vec2(1)))){
            vec2 size=vec2(textureSize(aerialShadowAtlas,0)),at=(cloudShadowRect.xy+uv*cloudShadowRect.zw)*size-.5;
            ivec2 lo=ivec2(round(cloudShadowRect.xy*size)),hi=lo+ivec2(round(cloudShadowRect.zw*size))-1,b=ivec2(floor(at));vec2 f=fract(at);
            float t=mix(mix(texelFetch(aerialShadowAtlas,clamp(b,lo,hi),0).r,texelFetch(aerialShadowAtlas,clamp(b+ivec2(1,0),lo,hi),0).r,f.x),mix(texelFetch(aerialShadowAtlas,clamp(b+ivec2(0,1),lo,hi),0).r,texelFetch(aerialShadowAtlas,clamp(b+ivec2(1),lo,hi),0).r,f.x),f.y);
            visibility*=t;
        }
    }
    return visibility;
}
