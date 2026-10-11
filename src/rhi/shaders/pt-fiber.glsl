#ifndef FIBER_INLINE
#define FIBER_INLINE
#endif
// Shared scalar/vector equations compiled by GLSL and the CPU reference.
// Hair: circular-fiber R/TT/TRT + residual-order model described in PBRT 4e,
// https://pbr-book.org/4ed/Reflection_Models/Scattering_from_Hair .
// Cloth uses bounded grazing sheen and Oren-Nayar diffuse, with mixture energy allocation.
const float FIBER_PI=3.141592653589793f;
struct FiberHair {vec3 energy[4];vec4 variance;float logisticScale,gammaO,gammaT,sinO,cosO,phiO,tilt;};
FIBER_INLINE float fiberFresnel(float c,float eta){
    c=clamp(c,0.f,1.f);float ct=sqrt(max(0.f,1.f-(1.f-c*c)/(eta*eta)));
    float a=(eta*c-ct)/max(eta*c+ct,1e-8f),b=(c-eta*ct)/max(c+eta*ct,1e-8f);return .5f*(a*a+b*b);
}
FIBER_INLINE FiberHair fiberHair(vec3 outgoing,vec3 color,vec4 parameters,float offset,vec3 absorption){
    FiberHair h;h.sinO=clamp(outgoing.x,-1.f,1.f);h.cosO=sqrt(max(0.f,1.f-h.sinO*h.sinO));h.phiO=atan(outgoing.z,outgoing.y);h.tilt=parameters.w;
    float bm=parameters.x,bn=parameters.y,eta=parameters.z;
    float width=.726f*bm+.812f*bm*bm+3.7f*pow(bm,20.f);h.variance=vec4(width*width)*vec4(1.f,.25f,4.f,4.f);
    h.logisticScale=.626657069f*(.265f*bn+1.194f*bn*bn+5.372f*pow(bn,22.f));
    offset=clamp(offset,-.999f,.999f);h.gammaO=asin(offset);
    float sinGT=offset*h.cosO/sqrt(max(eta*eta-h.sinO*h.sinO,1e-8f));h.gammaT=asin(clamp(sinGT,-1.f,1.f));
    vec3 sigma=dot(absorption,vec3(1))>0.f?absorption:-.5f*log(clamp(color,vec3(.0001f),vec3(1)));
    float cosTT=sqrt(max(0.f,1.f-h.sinO*h.sinO/(eta*eta)));
    vec3 transmittance=exp(-sigma*(2.f*sqrt(max(0.f,1.f-sinGT*sinGT))/max(cosTT,1e-6f)));
    float f=fiberFresnel(h.cosO*sqrt(1.f-offset*offset),eta);
    h.energy[0]=vec3(f);h.energy[1]=(1.f-f)*(1.f-f)*transmittance;
    h.energy[2]=h.energy[1]*f*transmittance;
    h.energy[3]=h.energy[2]*f*transmittance/max(vec3(1)-f*transmittance,vec3(1e-6f));return h;
}
FIBER_INLINE vec2 fiberTilt(FiberHair h,int order){
    float angle=order==0?-2.f*h.tilt:order==1?h.tilt:order==2?4.f*h.tilt:0.f;
    return vec2(h.sinO*cos(angle)+h.cosO*sin(angle),abs(h.cosO*cos(angle)-h.sinO*sin(angle)));
}
FIBER_INLINE float fiberLogBessel(float a){
    if(a>12.f)return a-.5f*log(2.f*FIBER_PI*a)+log(1.f+1.f/(8.f*a)+9.f/(128.f*a*a)+225.f/(3072.f*a*a*a));
    float sum=1.f,term=1.f;for(int j=1;j<=18;++j){term*=a*a/(4.f*float(j*j));sum+=term;}return log(sum);
}
FIBER_INLINE float fiberLongitudinal(float si,float ci,vec2 outgoing,float variance){
    float a=ci*outgoing.y/variance,b=si*outgoing.x/variance;
    float normalization=log(variance)+1.f/variance+log(max(1.f-exp(-2.f/variance),1e-8f));
    return exp(fiberLogBessel(a)-b-normalization);
}
FIBER_INLINE float fiberAzimuth(FiberHair h,float phi,int order){
    if(order==3)return 1.f/(2.f*FIBER_PI);
    float delta=phi-(2.f*float(order)*h.gammaT-2.f*h.gammaO+float(order)*FIBER_PI);
    delta-=2.f*FIBER_PI*floor((delta+FIBER_PI)/(2.f*FIBER_PI));
    float e=exp(-abs(delta)/h.logisticScale),tail=exp(-FIBER_PI/h.logisticScale),normalization=(1.f-tail)/(1.f+tail);
    return e/(h.logisticScale*(1.f+e)*(1.f+e)*normalization);
}
FIBER_INLINE vec3 fiberHairKernel(FiberHair h,vec3 incoming){
    float si=clamp(incoming.x,-1.f,1.f),ci=sqrt(max(0.f,1.f-si*si)),phi=atan(incoming.z,incoming.y)-h.phiO;vec3 result=vec3(0);
    for(int k=0;k<4;++k)result+=h.energy[k]*fiberLongitudinal(si,ci,fiberTilt(h,k),h.variance[k])*fiberAzimuth(h,phi,k);
    return result;
}
FIBER_INLINE float fiberHairPdf(FiberHair h,vec3 incoming){
    float si=clamp(incoming.x,-1.f,1.f),ci=sqrt(max(0.f,1.f-si*si)),phi=atan(incoming.z,incoming.y)-h.phiO,total=0.f,result=0.f;
    for(int k=0;k<4;++k){float weight=dot(h.energy[k],vec3(1.f/3.f));total+=weight;result+=weight*fiberLongitudinal(si,ci,fiberTilt(h,k),h.variance[k])*fiberAzimuth(h,phi,k);}
    return result/max(total,1e-8f);
}
FIBER_INLINE vec3 fiberHairSample(FiberHair h,float choice,vec2 uv){
    float weights[4];float total=0.f;for(int k=0;k<4;++k){weights[k]=dot(h.energy[k],vec3(1.f/3.f));total+=weights[k];}
    choice*=total;int order=3;for(int k=0;k<3;++k){if(choice<weights[k]){order=k;break;}choice-=weights[k];}
    choice=clamp(choice/max(weights[order],1e-8f),.000001f,.999999f);
    vec2 tilted=fiberTilt(h,order);float variance=h.variance[order];
    float ct=clamp(1.f+variance*log(max(uv.x,.000001f)+(1.f-uv.x)*exp(-2.f/variance)),-1.f,1.f);
    float si=clamp(-ct*tilted.x+sqrt(max(0.f,1.f-ct*ct))*cos(2.f*FIBER_PI*uv.y)*tilted.y,-1.f,1.f);
    float delta=2.f*FIBER_PI*choice;
    if(order<3){float tail=exp(-FIBER_PI/h.logisticScale),cdfLow=tail/(1.f+tail),cdf=mix(cdfLow,1.f-cdfLow,choice);
        delta=2.f*float(order)*h.gammaT-2.f*h.gammaO+float(order)*FIBER_PI+h.logisticScale*log(cdf/(1.f-cdf));}
    float phi=h.phiO+delta,ci=sqrt(max(0.f,1.f-si*si));return vec3(si,ci*cos(phi),ci*sin(phi));
}
// An appearance approximation to stockinette: two curved V legs per cell.
// It modulates bounded color and reciprocal directional sheen; no displacement
// or opacity is invented. Explicit yarn geometry supplies close-up relief.
FIBER_INLINE vec4 fiberKnit(vec2 uv,vec4 settings){
    if(settings.x==0.f)return vec4(1,1,0,0);
    vec2 tile=uv*vec2(settings.y,settings.z),q=tile-floor(tile)-vec2(.5f);
    float width=.05f+.35f*(.5f+.5f*sin(FIBER_PI*q.y)),slope=.175f*FIBER_PI*cos(FIBER_PI*q.y);
    float distance=abs(abs(q.x)-width)/sqrt(1.f+slope*slope),section=clamp(distance/settings.w,0.f,1.f);
    float shade=.32f+.68f*sqrt(max(0.f,1.f-section*section));
    vec2 tangent=vec2((q.x<0.f?-1.f:1.f)*slope,1.f);tangent/=length(tangent);
    return vec4(mix(1.f,shade,settings.x),tangent.x,tangent.y,settings.x);
}
// Surface approximation for an opaque yarn bundle, not a fiber volume solver.
// The narrow axis follows the yarn tangent; Smith masking and dielectric
// Fresnel bound this single-scattering anisotropic GGX reflection.
FIBER_INLINE float fiberYarnLambda(vec3 d,vec2 tangent,float along,float across){
    float x=dot(vec2(d.x,d.y),tangent),y=dot(vec2(d.x,d.y),vec2(-tangent.y,tangent.x));
    return .5f*(sqrt(1.f+(along*along*x*x+across*across*y*y)/max(d.z*d.z,1e-12f))-1.f);
}
FIBER_INLINE float fiberYarnReflection(vec3 v,vec3 l,float roughness,vec2 tangent){
    vec3 h=(v+l)/max(length(v+l),1e-8f);
    float along=max(.08f,roughness*roughness*.3f),across=max(.16f,roughness*roughness);
    float hx=dot(vec2(h.x,h.y),tangent)/along,hy=dot(vec2(h.x,h.y),vec2(-tangent.y,tangent.x))/across;
    float q=hx*hx+hy*hy+h.z*h.z,D=1.f/(FIBER_PI*along*across*q*q);
    float G=1.f/(1.f+fiberYarnLambda(v,tangent,along,across)+fiberYarnLambda(l,tangent,along,across));
    float F=fiberFresnel(sqrt(clamp(.5f*(1.f+dot(v,l)),0.f,1.f)),1.55f);
    return D*G*F/max(4.f*v.z*l.z,1e-12f);
}
FIBER_INLINE float fiberClothTransmission(vec4 settings){
    return (1.f-settings.x)*(1.f-settings.w)*settings.z;
}
FIBER_INLINE vec3 fiberCloth(vec3 color,vec3 sheenColor,vec4 settings,float roughness,vec3 v,vec3 l,vec4 pattern){
    color*=pattern.x;
    if(pattern.w>0.f){vec2 t=vec2(pattern.y,pattern.z);
        float a=dot(vec2(v.x,v.y),t),b=dot(vec2(l.x,l.y),t);
        float alignment=.2f+.8f*sqrt(max(0.f,1.f-a*a))*sqrt(max(0.f,1.f-b*b));
        sheenColor*=mix(1.f,alignment,pattern.w);
    }
    float nv=v.z,nl=l.z,w=settings.x,t=settings.z;if(nv<=0.f||nl==0.f)return vec3(0);
    if(nl<0.f)return (1.f-w)*(1.f-settings.w)*t*color/FIBER_PI;
    float sigma=roughness*FIBER_PI*.5f,s2=sigma*sigma,A=1.f-.5f*s2/(s2+.33f),B=.45f*s2/(s2+.09f);
    float sv=sqrt(max(0.f,1.f-nv*nv)),sl=sqrt(max(0.f,1.f-nl*nl));
    float azimuth=max(0.f,dot(vec2(v.x,v.y),vec2(l.x,l.y))/max(sv*sl,1e-8f));
    float oren=A+B*azimuth*(nv<nl?sv*sl/max(nl,1e-6f):sl*sv/max(nv,1e-6f));
    float halfLength=length(v+l),vh=halfLength>1e-8f?dot(v,(v+l)/halfLength):1.f;
    float sheen=pow(clamp(1.f-vh,0.f,1.f),mix(8.f,2.f,settings.y));
    vec3 body=((1.f-w)*(1.f-t)*color*oren)/FIBER_PI;
    if(settings.w>0.f){
        vec2 tangent=pattern.w>0.f?vec2(pattern.y,pattern.z):vec2(1,0);
        body=(1.f-settings.w)*body+(1.f-w)*settings.w*vec3(fiberYarnReflection(v,l,roughness,tangent));
    }
    return body+w*sheenColor*sheen/FIBER_PI;
}
