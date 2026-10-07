// A single atlas stores eight horizontal flux planes. Sun-aligned coordinates
// keep a straight refracted beam aligned between depths; never interpolate
// across adjacent atlas tiles. Outside this finite field, retain uniform light.
float waterSunShaftFlux(vec3 point,sampler2D field,vec4 footprint,vec4 projection,float strength){
    if(strength<=0.||footprint.z<=0.)return 1.;
    float depth=projection.z-point.y;if(depth<=0.||depth>=32.)return 1.;
    vec2 coordinate=point.xz-projection.xy*(point.y-projection.z),uv=(coordinate-footprint.xy)/footprint.z;
    float edge=min(min(uv.x,uv.y),min(1.-uv.x,1.-uv.y));if(edge<=0.)return 1.;
    const float depths[8]=float[8](0.,1.5,3.,5.,8.,12.,20.,32.);int lower=0;
    for(int i=1;i<7;++i)if(depth>=depths[i])lower=i;
    float blend=clamp((depth-depths[lower])/(depths[lower+1]-depths[lower]),0.,1.);
    vec2 local=clamp(uv,vec2(.5/256.),vec2(1.-.5/256.));
    float a=textureLod(field,vec2((local.x+float(lower))/8.,local.y),0).r,b=textureLod(field,vec2((local.x+float(lower+1))/8.,local.y),0).r;
    float flux=mix(a,b,blend),fade=smoothstep(0.,.12,edge)*(1.-smoothstep(24.,32.,depth))*smoothstep(0.,.5,depth);
    // The strength is a contrast control around unit irradiance, not an
    // additional scattering source. Bound rare focal singularities.
    return mix(1.,clamp(1.+strength*(flux-1.),0.,12.),fade);
}
