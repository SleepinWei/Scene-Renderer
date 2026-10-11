#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=0) out vec4 colour;
layout(set=0,binding=0,std140) uniform AirFogParameters {mat4 airInverseVP;vec4 airCamera;vec4 airSettings;vec4 airColour;vec4 airAerial;};
layout(set=0,binding=1) uniform sampler2D airSceneDepth;
layout(set=0,binding=2) uniform sampler2D airSceneColour;
layout(set=0,binding=3) uniform sampler2D aerialRadiance;
layout(set=0,binding=4) uniform sampler2D aerialTransmission;
layout(set=0,binding=5) uniform sampler2D aerialSolarLoss;
layout(set=0,binding=6) uniform sampler2D airSceneMotion;
#include "air-segment.glsl"
vec3 aerialLookup(sampler2D volume,vec2 uv,float distanceMeters){
    float z=sqrt(clamp(distanceMeters/airAerial.x,0.,1.))*31.;float low=floor(z),high=min(low+1.,31.);
    vec2 p=clamp(uv*32.-.5,vec2(0),vec2(31))+.5;
    return mix(texture(volume,vec2(p.x/32.,(p.y+low*32.)/1024.)).rgb,
               texture(volume,vec2(p.x/32.,(p.y+high*32.)/1024.)).rgb,z-low);
}
void main(){
    float z=texture(airSceneDepth,uv).r;vec4 point=airInverseVP*vec4(uv*vec2(2,-2)+vec2(-1,1),min(z,.999999),1);
    vec3 delta=point.xyz/point.w;if(z>=.999999)delta=normalize(delta)*airCamera.w;
    vec3 radiance=texture(airSceneColour,uv).rgb;
    // The sky LUT has already integrated the atmosphere. Only finite geometry
    // receives the camera-volume contribution; never fog the sky a second time.
    if(airAerial.x>0.&&z<.999999&&length(delta)>airAerial.y){float distanceMeters=length(delta);
        radiance=radiance*aerialLookup(aerialTransmission,uv,distanceMeters)+aerialLookup(aerialRadiance,uv,distanceMeters);}
    // Replace only local clear-air sunlight already present in SkyView. Cloud
    // pixels own their volume integration and mark motion.w=-1: leave them alone.
    if(airAerial.x>0.&&airAerial.z>.5&&z>=.999999&&texture(airSceneMotion,uv).w>=0.)
        radiance=max(radiance-aerialLookup(aerialSolarLoss,uv,airAerial.x),vec3(0));
    float T=exp(-min(airOpticalDepth(airCamera.xyz,airCamera.xyz+delta,airSettings),16.));
    colour=vec4(radiance*T+airColour.rgb*(1.-T),1);
}
