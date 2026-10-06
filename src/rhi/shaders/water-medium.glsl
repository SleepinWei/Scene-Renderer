// Shared eye-medium classification and transport. Macro waves are used for
// volume boundaries; subpixel ripples remain in interface shading.
#ifndef WATER_MEDIUM_VISIBILITY
#define WATER_MEDIUM_VISIBILITY(point) 1.
#endif
float waterMediumHeight(vec2 xz,sampler2D large,sampler2D shore,vec4 waves) {
    float height=waterMacroHeight(xz,large,waves);
    return coastHeight(xz,height,shore,coastPatch,waves.x,coastFeatures.z);
}
bool waterEyeInside(vec3 eye,sampler2D large,sampler2D shore,sampler2D mask,vec4 waves,float halfExtent) {
    if(max(abs(eye.x),abs(eye.z))>halfExtent)return false;
    vec4 state=coastSample(shore,eye.xz,coastPatch);
    float blend=coastFeatures.z>.5?coastBlend(eye.xz,state,coastPatch,waves.x):0.;
    bool wet=textureLod(mask,eye.xz/(halfExtent*2.)*vec2(1,-1)+.5,0).r>=.5;
    if(blend>.01){if(state.x<=.005||eye.y<state.w)return false;wet=true;}
    return wet&&eye.y<waterMediumHeight(eye.xz,large,shore,waves)-.002;
}
float waterDielectricFresnel(float cosine,float eta) {
    cosine=clamp(cosine,0.,1.);float sin2=eta*eta*(1.-cosine*cosine);
    if(sin2>=1.)return 1.;
    float transmitted=sqrt(max(1.-sin2,0.));
    float rs=(eta*cosine-transmitted)/max(eta*cosine+transmitted,1e-7);
    float rp=(cosine-eta*transmitted)/max(cosine+eta*transmitted,1e-7);
    return .5*(rs*rs+rp*rp);
}
vec3 waterMediumTransport(vec3 background,vec3 origin,vec3 ray,float distance,
                         vec3 L,vec3 solar,vec3 sky,vec3 sigmaA,vec3 sigmaS,float g,float strength,
                         sampler2D large,sampler2D shore,vec4 waves) {
    vec3 sigma=max(sigmaA+sigmaS,vec3(0)),T=exp(-sigma*distance);
    if(strength<=0.||dot(sigmaS,sigmaS)<1e-12)return background*T;
    vec3 towardSun=-refract(-L,vec3(0,1,0),1./1.333);
    towardSun=dot(towardSun,towardSun)>1e-8?normalize(towardSun):vec3(0,1,0);
    float cosine=dot(towardSun,ray);
    float phase=(1.-g*g)/(12.5663706144*pow(max(1.+g*g-2.*g*cosine,1e-4),1.5));
    float sunF=waterDielectricFresnel(max(L.y,0.),1./1.333);
    float ds=distance/4.;vec3 weight=sigmaS/max(sigma,vec3(1e-7))*(1.-exp(-sigma*ds));
    vec3 accumulated=vec3(0);
    for(int i=0;i<4;++i){
        float t=(float(i)+.5)*ds;vec3 point=origin+ray*t;
        float depth=max(waterMediumHeight(point.xz,large,shore,waves)-point.y,0.);
        vec3 source=sky*.25*exp(-sigma*depth*1.5);
        if(L.y>0.)source+=solar*phase*(1.-sunF)*exp(-sigma*depth/max(towardSun.y,.05))*WATER_MEDIUM_VISIBILITY(point);
        accumulated+=exp(-sigma*(float(i)*ds))*weight*source;
    }
    return background*T+strength*accumulated;
}
