bool causticBed(vec2 xz,out float height) {
    vec3 local=(coastInverse*vec4(xz.x,0,xz.y,1)).xyz;vec2 uv=local.xz*.5+.5;
    if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))return false;
    vec2 size=vec2(textureSize(causticBedMap,0));float h=textureLod(causticBedMap,(uv*(size-1.)+.5)/size,0).r;
    height=(coastModel*vec4(local.x,h,local.z,1)).y;return true;
}
