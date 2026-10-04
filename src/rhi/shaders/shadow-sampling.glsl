#include "shadow-data.glsl"
layout(set=1,binding=5) uniform sampler2D shadowAtlas;
layout(set=1,binding=7) uniform sampler2D rsmFlux;
layout(set=0,binding=4) uniform sampler2D rsmPosition;
layout(set=0,binding=5) uniform sampler2D rsmNormal;
#include "shadow-visibility.glsl"
vec3 indirectRsm(vec3 p,vec3 N,vec3 albedo,float metallic){
    if(shadowSettings.z<=0. || rsmRect.z<=0.)return vec3(0);
    vec4 clip=rsmMatrix*vec4(p,1);if(clip.w<=0.)return vec3(0);
    vec2 center=clip.xy/clip.w*vec2(.5,-.5)+.5;vec3 irradiance=vec3(0);
    int samples=clamp(int(rsmSettings.y),1,256);float radius=clamp(rsmSettings.x,.001,1.);
    for(int i=0;i<samples;++i){
        float angle=float(i)*2.39996323;vec2 uv=center+vec2(cos(angle),sin(angle))*sqrt((float(i)+.5)/float(samples))*radius;
        if(any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1))))continue;
        vec2 coord=rsmRect.xy+uv*rsmRect.zw;vec4 source=texture(rsmPosition,coord);if(source.w<.5)continue;
        vec3 delta=p-source.xyz;float d2=dot(delta,delta);if(d2<1e-8)continue;vec3 L=delta*inversesqrt(d2);
        float geometry=max(dot(normalize(texture(rsmNormal,coord).xyz),L),0.)*max(dot(N,-L),0.)/max(d2,rsmSettings.z*rsmSettings.z);
        irradiance+=texture(rsmFlux,coord).rgb*geometry/3.14159265;
    }
    float represented=3.14159265*radius*radius*shadowSettings.w*shadowSettings.w;
    return shadowSettings.z*irradiance*represented/float(samples)*albedo*(1.-metallic)/3.14159265;
}
