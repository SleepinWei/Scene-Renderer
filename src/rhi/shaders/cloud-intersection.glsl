vec3 cloudRay(vec2 uv) {
    vec4 w=cloudInverseVP*vec4(uv*vec2(2,-2)+vec2(-1,1),0,1);
    return normalize(w.xyz/w.w);
}
vec2 cloudSphere(vec3 origin,vec3 direction,float radius) {
    float b=dot(origin,direction),h=length(origin);
    float c=(h-radius)*(h+radius),d=b*b-c;
    if(d<0.)return vec2(1e20,-1e20);
    float s=sqrt(d),q=-b-(b>=0.?s:-s),a=abs(q)>1e-6?c/q:-b-s;
    return vec2(min(a,q),max(a,q));
}
vec2 cloudInterval(vec3 direction,float surface) {
    if(cloudVolumeCenterMode.w>.5){
        vec3 center=cloudVolumeCenterMode.xyz+vec3(cloudWindHistory.x,0,cloudWindHistory.y)*cloudCameraTime.w;
        vec3 o=cloudCameraTime.xyz-center,extent=cloudVolumeSize.xyz*.5;
        vec3 safe=vec3(abs(direction.x)<1e-8?1e-8:direction.x,abs(direction.y)<1e-8?1e-8:direction.y,abs(direction.z)<1e-8?1e-8:direction.z);
        vec3 a=(-extent-o)/safe,b=(extent-o)/safe;
        vec3 lo=min(a,b),hi=max(a,b);float start=max(max(lo.x,lo.y),max(lo.z,0.));float end=min(min(hi.x,hi.y),min(hi.z,min(surface,cloudShape.w)));
        vec2 earth=cloudSphere(cloudCameraTime.xyz-vec3(0,cloudPlanet.y-cloudPlanet.x,0),direction,cloudPlanet.x);
        if(earth.x>0.)end=min(end,earth.x);
        return vec2(start,end);
    }
    vec3 origin=cloudCameraTime.xyz-vec3(0,cloudPlanet.y-cloudPlanet.x,0);
    float h=length(origin),inner=cloudPlanet.x+cloudLayer.x,outer=inner+cloudLayer.y;
    vec2 hi=cloudSphere(origin,direction,outer),lo=cloudSphere(origin,direction,inner);
    float start=max(hi.x,0.),end=hi.y;
    if(h<inner)start=max(start,lo.y);
    else if(lo.x>start)end=min(end,lo.x);
    vec2 earth=cloudSphere(origin,direction,cloudPlanet.x);
    if(earth.x>0.)end=min(end,earth.x);
    end=min(end,min(surface,cloudShape.w));
    return vec2(start,end);
}
float cloudSurface(vec2 uv,float z) {
    if(z>=.999999)return cloudShape.w;
    vec4 p=cloudInverseVP*vec4(uv*vec2(2,-2)+vec2(-1,1),z,1);
    return length(p.xyz/p.w);
}
