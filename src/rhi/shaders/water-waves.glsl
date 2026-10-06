// FFT data is stored at texel centres. Invert horizontal displacement before
// querying height at world-space coordinates (three fixed-point steps).
vec2 waterWaveUV(vec2 xz,float domain,sampler2D displacement) {
    return xz/domain+.5+.5/vec2(textureSize(displacement,0));
}
vec3 waterDisplacement(vec2 xz,sampler2D large,sampler2D detail,vec4 waves) {
    vec3 d=textureLod(large,waterWaveUV(xz,waves.y,large),0).xyz;
    if(waves.w>0)d+=textureLod(detail,waterWaveUV(xz,waves.z,detail),0).xyz;
    return d;
}
float waterHeight(vec2 worldXZ,sampler2D large,sampler2D detail,vec4 waves) {
    vec2 q=worldXZ;
    for(int i=0;i<3;++i)q=worldXZ-waterDisplacement(q,large,detail,waves).xz;
    return waves.x+waterDisplacement(q,large,detail,waves).y;
}
// Bounded upward ray/surface thickness estimate. Steep, folded waves may have
// multiple roots; this does not model overturning breakers.
float waterSunDistance(vec3 point,vec3 towardSun,sampler2D large,sampler2D detail,vec4 waves,float limit) {
    if(towardSun.y<=.05)return limit;
    float t=clamp((waterHeight(point.xz,large,detail,waves)-point.y)/towardSun.y,0,limit);
    for(int i=0;i<3;++i){vec3 q=point+towardSun*t;
        t=clamp(t+(waterHeight(q.xz,large,detail,waves)-q.y)/towardSun.y,0,limit);}
    return t;
}

// Volume transport uses the macro FFT surface: subpixel ripple thickness has
// little optical effect but would multiply texture work at every ray sample.
float waterMacroHeight(vec2 worldXZ,sampler2D large,vec4 waves) {
    vec2 q=worldXZ;
    for(int i=0;i<2;++i)q=worldXZ-textureLod(large,waterWaveUV(q,waves.y,large),0).xz;
    return waves.x+textureLod(large,waterWaveUV(q,waves.y,large),0).y;
}
float waterVolumeSunDistance(vec3 point,vec3 towardSun,sampler2D large,vec4 waves,float limit,out float verticalDepth) {
    verticalDepth=max(waterMacroHeight(point.xz,large,waves)-point.y,0.);
    if(towardSun.y<=.05)return limit;
    float t=clamp(verticalDepth/towardSun.y,0,limit);
    for(int i=0;i<2;++i){vec3 q=point+towardSun*t;
        t=clamp(t+(waterMacroHeight(q.xz,large,waves)-q.y)/towardSun.y,0,limit);}
    return t;
}
