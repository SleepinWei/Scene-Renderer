vec2 cloudNoiseSample(vec3 uv) {
    vec3 p=fract(uv)*64.;float z=floor(p.z);vec2 value=vec2(0);
    for(int i=0;i<2;i++) {
        int slice=(int(z)+i)%64;vec2 tile=vec2(slice%8,slice/8);
        vec2 at=(tile*66.+1.+p.xy+.5)/528.;
        value+=textureLod(cloudNoiseAtlas,at,0).rg*(i==0?1.-fract(p.z):fract(p.z));
    }return value;
}
float cloudDensity(vec3 p) {
    vec3 center=vec3(0,cloudPlanet.y-cloudPlanet.x,0);
    float altitude=length(p-center)-cloudPlanet.x;
    float h=(altitude-cloudLayer.x)/cloudLayer.y;if(h<=0. || h>=1.)return 0.;
    vec2 shift=cloudWindHistory.xy*cloudCameraTime.w;
    vec2 weather=textureLod(cloudWeather,fract((p.xz-shift)/cloudShape.y),0).rg;
    float coverage=clamp((weather.x-.5)*1.8+(cloudLayer.z-.5)*1.6+.5,0.,1.);
    float profile=smoothstep(0.,.14,h)*(1.-smoothstep(.55+.25*weather.y,1.,h));
    float threshold=1.-coverage+(1.-profile)*.18;
    if(threshold>=1.)return 0.; // Cheap weather/profile rejection before 3D noise.
    vec2 noise=cloudNoiseSample((p-vec3(shift.x,0,shift.y))/cloudShape.x);
    float base=clamp((noise.x-.2)*1.7,0.,1.);
    float shape=max((base-threshold)/max(coverage,.05),0.);
    // Stronger edge erosion adds visible small-scale structure without an
    // extra atlas fetch or a larger secondary-light budget.
    return max(shape-(1.-noise.y)*cloudShape.z*.9,0.)*profile;
}
