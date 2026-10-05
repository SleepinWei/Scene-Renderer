vec2 cloudNoiseSample(vec3 uv) {
    vec3 p=fract(uv)*64.;float z=floor(p.z);vec2 value=vec2(0);
    for(int i=0;i<2;i++) {
        int slice=(int(z)+i)%64;vec2 tile=vec2(slice%8,slice/8);
        vec2 at=(tile*66.+1.+p.xy+.5)/528.;
        value+=textureLod(cloudNoiseAtlas,at,0).rg*(i==0?1.-fract(p.z):fract(p.z));
    }return value;
}
