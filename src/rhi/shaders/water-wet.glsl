#ifdef WATER_WET_ENABLED
layout(set=0,binding=4,std140) uniform WaterWetParameters {vec4 wetPatch;vec4 wetOptions;};
layout(set=0,binding=5) uniform sampler2D waterWetHistory;
vec4 waterGroundHistory() {
    vec2 at=(worldPosition.xz-wetPatch.xy)/max(wetPatch.z,.001);
    if(wetOptions.x<.5||any(lessThan(at,vec2(0)))||any(greaterThan(at,vec2(1)))||normalize(worldNormal).y<.4)return vec4(0);
    vec4 history=texture(waterWetHistory,at);
    history.x*=wetOptions.y;
    float match=1.-smoothstep(.1,.5,abs(worldPosition.y-history.w));
    float edge=min(min(at.x,at.y),min(1.-at.x,1.-at.y));match*=smoothstep(0.,8.*wetPatch.w/wetPatch.z,edge);
    return vec4(history.xyz,match);
}
#else
vec4 waterGroundHistory(){return vec4(0);}
#endif
float groundWetness(){vec4 h=waterGroundHistory();return h.y*h.w;}
float groundFoam(){vec4 h=waterGroundHistory();return h.x*h.w*(1.-smoothstep(.005,.05,h.z));}
