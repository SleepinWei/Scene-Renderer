uint cloudHash(uvec3 p) {uint h=p.x*1597334677u^p.y*3812015801u^p.z*2798796415u;h^=h>>16;h*=2246822519u;h^=h>>13;return h;}
float cloudRandom(ivec3 p,int period) {
    p=(p%period+period)%period;
    return float(cloudHash(uvec3(p)+uvec3(uint(cloudQuality.z))))/4294967295.;
}
float cloudValue(vec3 p,int period) {
    ivec3 cell=ivec3(floor(p));vec3 f=fract(p);f=f*f*(3.-2.*f);float result=0.;
    for(int z=0;z<2;z++)for(int y=0;y<2;y++)for(int x=0;x<2;x++) {
        vec3 w=mix(1.-f,f,vec3(x,y,z));result+=cloudRandom(cell+ivec3(x,y,z),period)*w.x*w.y*w.z;
    }return result;
}
float cloudWorley(vec3 p,int period) {
    ivec3 cell=ivec3(floor(p));vec3 f=fract(p);float d=2.;
    for(int z=-1;z<=1;z++)for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++) {
        ivec3 offset=ivec3(x,y,z),c=cell+offset;
        vec3 point=vec3(cloudRandom(c,period),cloudRandom(c+ivec3(19,37,11),period),cloudRandom(c+ivec3(43,7,29),period));
        d=min(d,length(vec3(offset)+point-f));
    }return 1.-clamp(d,0.,1.);
}
