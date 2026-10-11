float airSegmentTau(vec3 p,vec3 q,vec4 settings){
    float h0=max(p.y-settings.z,0.),h1=max(q.y-settings.z,0.),d=abs(h1-h0)/settings.y;
    float integral=abs(d)<1e-4?1.-d*.5:(1.-exp(-d))/d;
    return settings.x*length(q-p)*exp(-min(h0,h1)/settings.y)*integral;
}
float airOpticalDepth(vec3 a,vec3 b,vec4 settings){
    if(a.y<=settings.w&&b.y<=settings.w)return 0.;
    if(a.y<settings.w)a=mix(a,b,(settings.w-a.y)/(b.y-a.y));
    else if(b.y<settings.w)b=mix(a,b,(settings.w-a.y)/(b.y-a.y));
    if((a.y-settings.z)*(b.y-settings.z)<0.){vec3 m=mix(a,b,(settings.z-a.y)/(b.y-a.y));return airSegmentTau(a,m,settings)+airSegmentTau(m,b,settings);}
    return airSegmentTau(a,b,settings);
}
