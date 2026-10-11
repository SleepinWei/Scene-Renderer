#include "dielectric-fresnel.glsl"
const float WATER_PI=3.14159265359;
void waterSolarBasis(vec3 L,out vec3 t,out vec3 b){
    t=normalize(cross(abs(L.y)<.99?vec3(0,1,0):vec3(0,0,1),L));b=cross(L,t);
}
// Normalized angular radiance / normal-incidence solar irradiance. The
// unresolved branch approximates a finite disk with an anisotropic Gaussian;
// its peak matches the unfiltered disk and its angular integral preserves flux.
float waterSolarAngularKernelCov(vec3 ray,vec3 L,float radius,vec3 dx,vec3 dy,mat2 unresolved){
    if(dot(ray,L)<=0.)return 0.;
    radius=max(radius,1e-5);vec3 t,b;waterSolarBasis(L,t,b);
    float angle=atan(length(cross(ray,L)),clamp(dot(ray,L),-1.,1.));
    vec2 q=vec2(dot(ray,t),dot(ray,b));float sn=length(q);q*=sn>1e-8?angle/sn:1.;
    vec2 x=vec2(dot(dx,t),dot(dx,b)),y=vec2(dot(dy,t),dot(dy,b));
    float cxx=(x.x*x.x+y.x*y.x)/12.+unresolved[0][0],cyy=(x.y*x.y+y.y*y.y)/12.+unresolved[1][1],cxy=(x.x*x.y+y.x*y.y)/12.+unresolved[0][1];
    float largest=.5*(cxx+cyy+sqrt(max(0.,(cxx-cyy)*(cxx-cyy)+4.*cxy*cxy)));
    float sigma=sqrt(max(largest,0.)),width=min(2.*sigma,radius);
    float disk=width>1e-7?1.-smoothstep(radius-width*.5,radius+width*.5,angle):(angle<radius?1.:0.);
    // The smooth disk edge increases its planar area by pi*width^2/20.
    disk/=WATER_PI*(sin(radius)*sin(radius)+width*width/20.);
    float base=sin(radius)*sin(radius)*.5;cxx+=base;cyy+=base;
    float determinant=max(cxx*cyy-cxy*cxy,1e-20);
    float quadratic=(cyy*q.x*q.x-2.*cxy*q.x*q.y+cxx*q.y*q.y)/determinant;
    float gaussian=exp(-.5*max(quadratic,0.))/(2.*WATER_PI*sqrt(determinant));
    return mix(disk,gaussian,smoothstep(radius*.25,radius,sigma));
}
float waterSolarAngularKernel(vec3 ray,vec3 L,float radius,vec3 dx,vec3 dy){
    return waterSolarAngularKernelCov(ray,L,radius,dx,dy,mat2(0));
}
float waterSmith(float c,float roughness){
    if(c<=0.)return 0.;float a=roughness*roughness;
    return 2.*c/(c+sqrt(a*a+(1.-a*a)*c*c));
}
float waterRoughReflection(vec3 N,vec3 V,vec3 L,float roughness){
    float nv=dot(N,V),nl=dot(N,L);if(nv<=0.||nl<=0.)return 0.;
    vec3 H=normalize(V+L);float nh=clamp(dot(N,H),0.,1.),a=roughness*roughness,a2=a*a;
    float q=max(0.,1.-nh*nh)+a2*nh*nh;
    float D=a2/(WATER_PI*q*q),G=waterSmith(nv,roughness)*waterSmith(nl,roughness);
    return dielectricFresnel(dot(V,H),1.,1.333)*D*G/(4.*nv*nl);
}
vec3 waterRoughSun(vec3 N,vec3 V,vec3 L,vec3 irradiance,float radius,float roughness){
    vec3 t,b;waterSolarBasis(L,t,b);float total=0.;
    // Uniform spherical-cap quadrature. Irradiance was measured perpendicular
    // to the cap centre, so Lsun * solidAngle = E * 2/(1+cos(radius)).
    for(int i=0;i<16;++i){
        float u=(float(i)+.5)/16.,phi=float(i)*2.39996322973;
        float z=mix(1.,cos(radius),u),r=sqrt(max(0.,1.-z*z));
        vec3 wi=L*z+r*(t*cos(phi)+b*sin(phi));
        total+=waterRoughReflection(N,V,wi,roughness)*max(dot(N,wi),0.);
    }
    return irradiance*(total/16.)*(2./(1.+cos(radius)));
}

// Integrate the normal-map slope distribution at the required half vector.
// The exact ds / d(omega_reflected) Jacobian is 1/(4*VH*Hy^3). Unlike
// linearizing reflection at the mean normal, this stays valid at grazing views.
// This distribution describes shading normals per visible pixel, not a
// microfacet area NDF: no extra projected-area factor or Smith term is inserted.
vec3 waterSmoothSunSlopes(vec3 V,vec3 L,vec3 irradiance,float radius,vec2 mean,mat2 covariance){
    if(L.y<=0.)return vec3(0);
    float det=determinant(covariance);if(det<=1e-16)return vec3(0);
    mat2 inverseCov=inverse(covariance);vec3 t,b;waterSolarBasis(L,t,b);float total=0.;
    for(int i=0;i<16;++i){
        float u=(float(i)+.5)/16.,phi=float(i)*2.39996322973;
        float z=mix(1.,cos(radius),u),r=sqrt(max(0.,1.-z*z));vec3 wi=L*z+r*(t*cos(phi)+b*sin(phi));
        vec3 sum=V+wi;if(dot(sum,sum)<1e-10)continue;vec3 H=normalize(sum);
        if(H.y<=0.)continue;float vh=dot(V,H);if(vh<=0.)continue;
        vec2 delta=H.xz/H.y-mean;
        float density=exp(-.5*max(dot(delta,inverseCov*delta),0.))/(2.*WATER_PI*sqrt(det));
        total+=dielectricFresnel(vh,1.,1.333)*density/(4.*vh*H.y*H.y*H.y);
    }
    return irradiance*(total/16.)*(2./(1.+cos(radius)));
}

// GGX convolution approximation: add unresolved normal-slope covariance to
// the intrinsic alpha^2 tensor. GGX has no finite second moment; this tensor
// addition is a filtering closure, not an exact convolution of the two NDFs.
vec3 waterRoughSunMoments(vec3 N,vec3 V,vec3 L,vec3 irradiance,float radius,float roughness,vec2 slope,mat2 covariance){
    vec3 t,b;waterSolarBasis(N,t,b);float inverseLength=inversesqrt(1.+dot(slope,slope));
    vec3 nx=(vec3(1,0,0)-N*N.x)*inverseLength,nz=(vec3(0,0,1)-N*N.z)*inverseLength;
    mat2 J=mat2(vec2(dot(nx,t),dot(nx,b)),vec2(dot(nz,t),dot(nz,b)));
    mat2 M=mat2(pow(roughness,4.))+2.*J*covariance*transpose(J),invM=inverse(M);
    float detM=max(determinant(M),1e-12),nv=dot(N,V);if(nv<=0.)return vec3(0);
    vec2 vt=vec2(dot(V,t),dot(V,b));float gv=2./(1.+sqrt(1.+dot(vt,M*vt)/(nv*nv)));
    vec3 st,sb;waterSolarBasis(L,st,sb);float total=0.;
    for(int i=0;i<16;++i){float u=(float(i)+.5)/16.,phi=float(i)*2.39996322973;
        float z=mix(1.,cos(radius),u),r=sqrt(max(0.,1.-z*z));vec3 wi=L*z+r*(st*cos(phi)+sb*sin(phi));
        float nl=dot(N,wi);if(nl<=0.)continue;
        vec3 H=normalize(V+wi);float nh=dot(N,H);vec2 ht=vec2(dot(H,t),dot(H,b)),lt=vec2(dot(wi,t),dot(wi,b));
        float q=nh*nh+dot(ht,invM*ht),D=1./(WATER_PI*sqrt(detM)*q*q);
        float gl=2./(1.+sqrt(1.+dot(lt,M*lt)/(nl*nl)));
        total+=dielectricFresnel(dot(V,H),1.,1.333)*D*gv*gl/(4.*nv);
    }
    return irradiance*(total/16.)*(2./(1.+cos(radius)));
}
