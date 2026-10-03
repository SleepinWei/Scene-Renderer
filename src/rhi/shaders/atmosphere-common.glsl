const float ATM_PI=3.141592653589793;
struct AtmosphereParameters {
    float solar_irradiance;float sun_angular_radius;float top_radius;float bottom_radius;
    float HDensityRayleigh;float HDensityMie;float OzoneCenter;float mie_g;
    vec3 rayleigh_scattering;vec3 mie_scattering;vec3 mie_extinction;vec3 absorption_extinction;float OzoneWidth;
};
layout(set=0,binding=0,std140) uniform ATMOS {AtmosphereParameters atmosphere;};
layout(set=0,binding=1,std140) uniform LutParameters {
    ivec4 dimensions;vec4 sunDirectionHeight;vec4 solarColorMultiple;vec4 lutSettings;
};
float horizonCosine(float r) {float s=atmosphere.bottom_radius/r;return -sqrt(max(0.0,1.0-s*s));}
bool intersectsGround(float r,float mu) {return mu<horizonCosine(r);}
float distanceTop(float r,float mu) {
    return max(0.0,-r*mu+sqrt(max(0.0,r*r*(mu*mu-1.0)+atmosphere.top_radius*atmosphere.top_radius)));
}
float distanceGround(float r,float mu) {
    return max(0.0,-r*mu-sqrt(max(0.0,r*r*(mu*mu-1.0)+atmosphere.bottom_radius*atmosphere.bottom_radius)));
}
float radiusAlongRay(float r,float mu,float t) {return sqrt(max(atmosphere.bottom_radius*atmosphere.bottom_radius,r*r+2.0*r*mu*t+t*t));}
vec3 densities(float altitude) {
    return vec3(exp(-max(altitude,0.0)/atmosphere.HDensityRayleigh),exp(-max(altitude,0.0)/atmosphere.HDensityMie),max(0.0,1.0-abs(altitude-atmosphere.OzoneCenter)/atmosphere.OzoneWidth));
}
void coefficients(float altitude,out vec3 rayleigh,out vec3 mie,out vec3 extinction) {
    vec3 density=densities(altitude);rayleigh=atmosphere.rayleigh_scattering*density.x;mie=atmosphere.mie_scattering*density.y;
    extinction=rayleigh+atmosphere.mie_extinction*density.y+atmosphere.absorption_extinction*density.z;
}
vec3 segmentWeight(vec3 extinction,float dt) {
    vec3 t=exp(-extinction*dt);
    return mix((vec3(1.0)-t)/max(extinction,vec3(1e-7)),vec3(dt),lessThan(extinction,vec3(1e-7)));
}
vec2 transmittanceUV(float r,float mu) {
    float H=sqrt(atmosphere.top_radius*atmosphere.top_radius-atmosphere.bottom_radius*atmosphere.bottom_radius);
    float rho=sqrt(max(0.0,(r-atmosphere.bottom_radius)*(r+atmosphere.bottom_radius)));
    float d=distanceTop(r,mu),dmin=atmosphere.top_radius-r,dmax=rho+H;
    vec2 x=clamp(vec2((d-dmin)/max(dmax-dmin,1e-5),rho/H),0.0,1.0);
    return .5/vec2(dimensions.zw)+x*(1.0-1.0/vec2(dimensions.zw));
}
vec3 sunTransmission(sampler2D transmittance,float r,float mu) {
    float horizon=horizonCosine(r),width=atmosphere.bottom_radius/r*atmosphere.sun_angular_radius;
    float visible=smoothstep(-width,width,mu-horizon);
    return texture(transmittance,transmittanceUV(r,max(mu,horizon))).rgb*visible;
}
float rayleighPhase(float mu) {mu=clamp(mu,-1.0,1.0);return 3.0/(16.0*ATM_PI)*(1.0+mu*mu);}
float miePhase(float mu) {
    float g=clamp(atmosphere.mie_g,-.999,.999);mu=clamp(mu,-1.0,1.0);
    return 3.0/(8.0*ATM_PI)*(1.0-g*g)/(2.0+g*g)*(1.0+mu*mu)/pow(max(1.0+g*g-2.0*g*mu,1e-6),1.5);
}
