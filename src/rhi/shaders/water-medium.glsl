// Shared eye-medium classification and transport. Macro waves are used for
// volume boundaries; subpixel ripples remain in interface shading.
#ifndef WATER_MEDIUM_VISIBILITY
#define WATER_MEDIUM_VISIBILITY(point) 1.
#endif
#ifndef WATER_MEDIUM_SOLAR_FLUX
#define WATER_MEDIUM_SOLAR_FLUX(point) 1.
#endif
#ifndef WATER_MEDIUM_STEPS
#define WATER_MEDIUM_STEPS 4
#endif
#ifdef WATER_PARTICLE_DENSITY
#define WATER_HAS_PARTICLES
#endif
#ifndef WATER_PARTICLE_DENSITY
#define WATER_PARTICLE_DENSITY 0.
#define WATER_PARTICLE_TIME 0.
#endif
// World-space cells carry one possible sediment mote. DDA visits cells along
// the actual water segment, so scene depth occludes them and camera rotation
// does not make them follow the screen. A pixel footprint softens tiny motes.
#ifdef WATER_HAS_PARTICLES
uint waterDustHash(uvec3 p) {
    uint h=p.x*1597334677u ^ p.y*3812015801u ^ p.z*2798796415u;
    h^=h>>16;h*=2246822519u;h^=h>>13;return h;
}
vec3 waterSuspendedParticles(vec3 origin,vec3 ray,float distance,vec3 sigma,vec3 light) {
    if(WATER_PARTICLE_DENSITY<=0.)return vec3(0);
    const float cellSize=.65;
    vec3 drift=vec3(.007,.014,.004)*WATER_PARTICLE_TIME;
    vec3 o=origin-drift;ivec3 cell=ivec3(floor(o/cellSize));
    vec3 signRay=sign(ray),inverseRay=1./max(abs(ray),vec3(1e-6));
    vec3 next=(vec3(cell)+step(vec3(0),ray))*cellSize;
    vec3 boundary=abs(next-o)*inverseRay,delta=cellSize*inverseRay;
    for(int axis=0;axis<3;++axis)if(abs(ray[axis])<1e-6)boundary[axis]=1e20;
    float footprint=length(fwidth(ray))*.5;
    vec3 result=vec3(0);float entry=0.;
    for(int i=0;i<48;++i) {
        if(entry>=min(distance,14.))break;
        uint h=waterDustHash(uvec3(cell));
        vec3 random=vec3(float(h&1023u),float((h>>10)&1023u),float((h>>20)&1023u))/1023.;
        if(float(waterDustHash(uvec3(cell)+17u)&65535u)/65535.<WATER_PARTICLE_DENSITY) {
            vec3 center=(vec3(cell)+mix(vec3(.12),vec3(.88),random))*cellSize+drift;
            float t=dot(center-origin,ray),radius=mix(.004,.010,random.y);
            float width=max(.001,footprint*max(t,.2));
            float separation=length(center-origin-ray*t);
            if(t>max(radius,.45)&&t<distance) {
                float coverage=1.-smoothstep(max(radius-width,0.),radius+width,separation);
                coverage*=min(1.,radius*radius/max(width*width,1e-6));
                result+=coverage*.10*light*exp(-sigma*t);
            }
        }
        int axis=boundary.x<boundary.y?(boundary.x<boundary.z?0:2):(boundary.y<boundary.z?1:2);
        entry=boundary[axis];boundary[axis]+=delta[axis];cell[axis]+=int(signRay[axis]);
    }
    return result;
}
#else
vec3 waterSuspendedParticles(vec3 origin,vec3 ray,float distance,vec3 sigma,vec3 light) {return vec3(0);}
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
    int steps=clamp(WATER_MEDIUM_STEPS,4,32);
    float ds=distance/float(steps);vec3 weight=sigmaS/max(sigma,vec3(1e-7))*(1.-exp(-sigma*ds));
    vec3 accumulated=vec3(0);
    for(int i=0;i<steps;++i){
        float t=(float(i)+.5)*ds;vec3 point=origin+ray*t;
        float depth=max(waterMediumHeight(point.xz,large,shore,waves)-point.y,0.);
        vec3 source=sky*.25*exp(-sigma*depth*1.5);
        if(L.y>0.)source+=solar*phase*(1.-sunF)*exp(-sigma*depth/max(towardSun.y,.05))*WATER_MEDIUM_VISIBILITY(point)*WATER_MEDIUM_SOLAR_FLUX(point);
        accumulated+=exp(-sigma*(float(i)*ds))*weight*source;
    }
    vec3 particles=vec3(0);
    if(WATER_PARTICLE_DENSITY>0.) {
        float originDepth=max(waterMediumHeight(origin.xz,large,shore,waves)-origin.y,0.);
        vec3 particleLight=(sky*.25+(L.y>0.?solar*.08:vec3(0)))*exp(-sigma*originDepth/max(towardSun.y,.05));
        particles=waterSuspendedParticles(origin,ray,distance,sigma,particleLight);
    }
    return background*T+strength*accumulated+particles;
}
