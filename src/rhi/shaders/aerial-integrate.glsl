vec3 aerialSampleMultiple(float r,float muSun) {
    vec2 size=vec2(textureSize(aerialMultiple,0));
    vec2 x=vec2(muSun*.5+.5,(r-atmosphere.bottom_radius)/(atmosphere.top_radius-atmosphere.bottom_radius));
    return texture(aerialMultiple,.5/size+clamp(x,0.,1.)*(1.-1./size)).rgb;
}
// Rayleigh/Mie single scattering plus the shared isotropic multi-scattering LUT.
// Integrate only the air segment; a submerged origin is clipped to mean water level.
void integrateAerial(vec3 origin,vec3 direction,float distanceMeters,float waterLevel,out vec3 L,out vec3 T,out vec3 solarLoss) {
    L=vec3(0);T=vec3(1);solarLoss=vec3(0);vec3 physicalT=vec3(1);
    if(aerialSolar.w<.5||aerialDomain.w<=0.)return;
    float begin=aerialDomain.y,end=min(distanceMeters,aerialDomain.z);
    if(origin.y<waterLevel){
        if(direction.y<=0.)return;
        begin=max(begin,(waterLevel-origin.y)/direction.y);
    }else if(direction.y<0.)end=min(end,(waterLevel-origin.y)/direction.y);
    if(end<=begin)return;
    // Local tangent at the clipped air-segment start. Units are kilometres.
    vec3 p=vec3(0.,atmosphere.bottom_radius+clamp((origin.y+direction.y*begin-aerialDomain.x)*.001,.001,atmosphere.top_radius-atmosphere.bottom_radius-.001),0.);
    float r=length(p),mu=direction.y;
    float lengthKm=min((end-begin)*.001,intersectsGround(r,mu)?distanceGround(r,mu):distanceTop(r,mu));
    if(lengthKm<=0.)return;
    vec3 sun=normalize(aerialSunMultiple.xyz);float nu=dot(direction,sun);
    const int steps=32;
    for(int i=0;i<steps;++i){
        float a=float(i)/float(steps),b=float(i+1)/float(steps);
        float start=a*a*lengthKm,finish=b*b*lengthKm;
        vec3 samplePoint=p+direction*(start+finish)*.5;
        float ri=length(samplePoint),muSun=clamp(dot(samplePoint,sun)/ri,-1.,1.);
        vec3 rayleigh,mie,extinction;coefficients(ri-atmosphere.bottom_radius,rayleigh,mie,extinction);
        vec3 source=(rayleigh*rayleighPhase(nu)+mie*miePhase(nu))*sunTransmission(aerialTransmittance,ri,muSun);
        vec3 worldPoint=origin+direction*(begin+(start+finish)*500.);
        float visibility=aerialSunVisibility(worldPoint);
        // SkyView already contains clear atmospheric transport. Track only the
        // missing single-sun radiance at physical scale for a local correction.
        float physicalStep=finish-start;
        solarLoss+=physicalT*segmentWeight(extinction,physicalStep)*source*(1.-visibility)*aerialSolar.rgb;
        physicalT*=exp(-extinction*physicalStep);
        source*=visibility;
        source+=(rayleigh+mie)*aerialSampleMultiple(ri,muSun)*aerialSunMultiple.w;
        float dt=(finish-start)*aerialDomain.w;
        L+=T*segmentWeight(extinction,dt)*source*aerialSolar.rgb;
        T*=exp(-extinction*dt);
    }
    L=max(L,vec3(0));T=clamp(T,vec3(0),vec3(1));
}
