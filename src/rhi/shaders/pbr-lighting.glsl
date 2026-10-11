#include "pbr-light-data.glsl"
const float PI = 3.14159265359;
float geometry(float dotValue, float roughness) {
    float k = (roughness + 1) * (roughness + 1) / 8;
    return dotValue / max(dotValue * (1-k) + k, 1e-6);
}
#ifndef RHI_VISIBILITY
#define RHI_VISIBILITY(light,position,N,L) 1.
#endif
#ifndef RHI_TWO_SIDED
#define RHI_TWO_SIDED false
#endif
vec2 matchedGgxAlpha(float roughness,float anisotropy){
    float a=roughness*roughness,aspect=sqrt(1.-.9*abs(anisotropy));
    vec2 alpha=vec2(a/aspect,a*aspect);return anisotropy<0?alpha.yx:alpha;
}
float matchedGgxD(vec3 H,vec3 N,vec3 T,vec3 B,vec2 alpha){
    float z=dot(N,H);if(z<=0)return 0.;
    vec3 h=vec3(dot(T,H)/alpha.x,dot(B,H)/alpha.y,z);float q=dot(h,h);
    return 1./(PI*alpha.x*alpha.y*q*q);
}
float matchedGgxG1(vec3 V,vec3 N,vec3 T,vec3 B,vec2 alpha){
    float z=dot(N,V);if(z<=0)return 0.;
    vec2 v=alpha*vec2(dot(T,V),dot(B,V));return 2.*z/(z+sqrt(z*z+dot(v,v)));
}
vec3 shadePbrExtended(vec3 position,vec3 N,vec3 T,vec3 albedo,float metallic,float roughness,float ao,vec3 emissive,vec4 lobes,float thickness) {
    vec3 deltaV = cameraAmbient.xyz - position;
    vec3 V = dot(deltaV,deltaV) > 1e-10 ? normalize(deltaV) : N;
    bool leaf=lobes.w>=2.;float leafWeight=leaf?clamp(lobes.w-2.,0.,1.):0.;
    if(leaf && dot(N,V)<0.)N=-N;
    vec3 ambientAlbedo=albedo*(1.-leafWeight);
    vec3 F0 = mix(vec3(.04), albedo, metallic);
    float matchedModel=floor(lobes.y/4.);lobes.y-=4.*matchedModel;
    vec3 B=normalize(cross(N,T));vec2 alpha=matchedGgxAlpha(roughness,lobes.z);
    #ifdef RHI_AMBIENT
    vec3 result;
    #ifdef RHI_AMBIENT_SHAPED
    result=matchedModel>0?RHI_AMBIENT_SHAPED(position,N,V,F0,roughness,ambientAlbedo,metallic,ao,T,B,alpha):RHI_AMBIENT(position,N,V,F0,roughness,ambientAlbedo,metallic,ao);
    result+=emissive;
    #else
    result=RHI_AMBIENT(position,N,V,F0,roughness,ambientAlbedo,metallic,ao)+emissive;
    #endif
#else
    vec3 result=cameraAmbient.w*ambientAlbedo*ao+emissive;
#endif
    #ifdef RHI_LEAF_AMBIENT
    if(leaf)result+=RHI_LEAF_AMBIENT(N,sqrt(max(albedo,vec3(0)))*leafWeight*.96,ao);
    #else
    if(leaf)result+=cameraAmbient.w*sqrt(max(albedo,vec3(0)))*leafWeight*ao;
    #endif
    for (int i = 0; i < counts.x; ++i) {
        Light light = lights[i];
        vec3 delta = light.positionType.w < .5 ? -light.directionOuter.xyz : light.positionType.xyz - position;
        if (dot(delta,delta) < 1e-10) continue;
        vec3 L = normalize(delta);vec3 halfVector = V + L;
        if (dot(halfVector,halfVector) < 1e-10) continue;
        vec3 H = normalize(halfVector);
        vec3 facingN=!leaf && RHI_TWO_SIDED && dot(N,L)<0.?-N:N;
        float dotL = max(dot(facingN,L),0), dotV = max(dot(facingN,V),0), dotH = max(dot(facingN,H),0);
        float a = roughness * roughness, a2 = a*a;
        float denominator = dotH*dotH*(a2-1)+1;
        float distribution=a2/max(PI*denominator*denominator,1e-7);
        if(abs(lobes.z)>.0001){vec3 B=normalize(cross(N,T));float at=max(roughness*(1.+lobes.z),.001),ab=max(roughness*(1.-lobes.z),.001);vec3 v=vec3(ab*dot(T,H),at*dot(B,H),at*ab*dotH);float w=at*ab/max(dot(v,v),1e-8);distribution=at*ab*w*w/PI;}
        vec3 F = F0 + (1-F0)*pow(clamp(1-dot(H,V),0,1),5);
        float G = geometry(dotV,roughness)*geometry(dotL,roughness);
        vec3 specular = distribution * G * F / (4*dotV*dotL+.0001);
        float cone = 1;
        if (light.positionType.w > 1.5) cone = clamp((dot(L,-light.directionOuter.xyz)-light.directionOuter.w) / max(light.colorInner.w-light.directionOuter.w,1e-5),0,1);
        vec3 brdf=(1.-F)*(1.-metallic)*albedo/PI+specular;
        if(matchedModel>0){
            float D=matchedGgxD(H,facingN,T,B,alpha),gv=matchedGgxG1(V,facingN,T,B,alpha),gl=matchedGgxG1(L,facingN,T,B,alpha);
            if(dotV<=0 || dotL<=0)brdf=vec3(0);
            else if(matchedModel>1.5){
                float G=gv*gl/max(gv+gl-gv*gl,1e-20);
                brdf=.96*albedo*(1.-metallic)/PI+F0*(D*G/(4.*dotV*dotL));
            } else brdf=(1.-F)*albedo*(1.-metallic)/PI+F*(D*gv*gl/(4.*dotV*dotL));
        }
        if(lobes.x>0){float coatA=pow(lobes.y,4.),coatA2=coatA*coatA,denom=dotH*dotH*(coatA2-1.)+1.;float Dc=coatA2/max(PI*denom*denom,1e-8),LoH=max(dot(L,H),1e-4),Fc=(.4+.6*pow(1.-LoH,5.))*.5*lobes.x;brdf=brdf*(1.-Fc)+vec3(Dc*.25/(LoH*LoH)*Fc);}
        vec3 direct=brdf*dotL;
        if(leaf){direct=(brdf-(1.-F)*(1.-metallic)*albedo/PI*leafWeight)*dotL+sqrt(max(albedo,vec3(0)))*leafWeight*.96/PI*max(-dot(N,L),0.);}
        if(!leaf && lobes.w>0){vec3 Hs=normalize(L+facingN*.2);float back=pow(clamp(dot(V,-Hs),0.,1.),100.);direct+=albedo*(back+cameraAmbient.w)*clamp(1.-thickness,0.,1.)*lobes.w;}
        float attenuation=light.positionType.w>.5 && counts.y>0?min(1.,1./max(dot(delta,delta),1e-6)):1.;
        result+=direct*light.colorInner.rgb*cone*attenuation*RHI_VISIBILITY(i,position,facingN,L);
    }
    return result;
}

vec3 shadePbr(vec3 position,vec3 N,vec3 albedo,float metallic,float roughness,float ao,vec3 emissive){vec3 T=normalize(abs(N.y)<.99?cross(N,vec3(0,1,0)):cross(N,vec3(1,0,0)));return shadePbrExtended(position,N,T,albedo,metallic,roughness,ao,emissive,vec4(0),0);}
