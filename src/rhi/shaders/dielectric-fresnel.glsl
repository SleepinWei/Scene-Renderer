#ifndef DIELECTRIC_FRESNEL_GLSL
#define DIELECTRIC_FRESNEL_GLSL
// CPU counterpart: pt::dielectricFresnel in PT/CpuPathTracer.cpp.
float dielectricFresnel(float cosine,float etaI,float etaT){
    cosine=clamp(abs(cosine),0,1);float st=etaI/etaT*sqrt(max(0,1-cosine*cosine));if(st>=1)return 1;
    float ct=sqrt(max(0,1-st*st)),a=(etaT*cosine-etaI*ct)/(etaT*cosine+etaI*ct),b=(etaI*cosine-etaT*ct)/(etaI*cosine+etaT*ct);return .5*(a*a+b*b);
}
#endif
