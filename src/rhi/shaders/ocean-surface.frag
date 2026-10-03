#version 450
#extension GL_GOOGLE_include_directive : require
#include "sky-mapping.glsl"
const float PI=3.14159265359;
layout(set=0,binding=1,std140) uniform WaterFragment {mat4 waterFragmentVP;mat4 view;vec4 camera;vec4 direction;vec4 diffuse;vec4 specular;vec4 shallow;vec4 deep;vec4 foamColor;vec4 specularColor;vec4 ambientColor;vec4 optics;vec4 volume;vec4 absorb;vec4 scatter;vec4 surface;vec4 flags;};
struct DirLight {vec3 direction;vec3 diffuse;vec3 specular;};
#define dirLight DirLight(direction.xyz,diffuse.xyz,specular.xyz)
#define viewPos camera.xyz
#define outer_OceanColorShallow shallow.xyz
#define outer_OceanColorDeep deep.xyz
#define outer_BubblesColor foamColor.xyz
#define outer_Specular specularColor.xyz
#define outer_ambient ambientColor.xyz
#define outer_FresnelScale optics.x
#define outer_Gloss int(optics.y)
#define refractionStrength optics.z
#define deepWaterDistance optics.w
#define subsurfaceStrength volume.x
#define scatteringAnisotropy volume.y
#define seaLevel surface.x
#define waveHeightScale surface.y
#define absorption absorb.xyz
#define scattering scatter.xyz
#define hasSky int(flags.x)
#define enableDetail int(flags.y)
#define enableRefraction int(flags.z)
#define temporalActive int(flags.w)
layout(location=0) in vec3 FragPos;layout(location=1) in vec2 FragTexCoord;layout(location=5) in vec2 DetailTexCoord;
layout(location=6) in vec4 PreviousClip;layout(location=7) in float PreviousDepth;
layout(location=0) out vec4 FragColor;layout(location=1) out vec4 TemporalMotion;
layout(set=1,binding=0) uniform sampler2D NormalRT;
layout(set=1,binding=1) uniform sampler2D BubblesRT;
layout(set=1,binding=2) uniform sampler2D skyview;
layout(set=1,binding=3) uniform sampler2D detailNormal;
layout(set=1,binding=4) uniform sampler2D detailFoam;
layout(set=1,binding=5) uniform sampler2D opaqueScene;
layout(set=1,binding=6) uniform sampler2D scenePosition;
layout(set=1,binding=7) uniform sampler2D sceneNormal;
vec3 skyRadiance(vec3 direction) {
    if(hasSky==0)return vec3(0);
    return sampleSkyLut(skyview,direction);
}
vec2 screenUV(vec3 point) {
    vec4 clip=waterFragmentVP*vec4(point,1);
    return clip.xy/max(clip.w,1e-5)*vec2(.5,-.5)+.5;
}
bool submerged(vec2 uv,vec3 surface,vec3 V) {
    if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))return false;
    vec3 p=texture(scenePosition,uv).xyz;
    return dot(texture(sceneNormal,uv).xyz,texture(sceneNormal,uv).xyz)>.25 &&
           p.y<surface.y-.02 && dot(p-surface,-V)>.01;
}
// Intersect the refracted ray with the visible opaque depth layer. A single
// depth-projected offset duplicates silhouettes when a sphere replaces the floor.
bool refractedUV(vec3 surface,vec3 V,vec3 R,float range,out vec2 hitUV) {
    float previous=0.0;
    for(int step=1;step<=24;++step) {
        float distance=range*float(step)/24.0;
        vec3 point=surface+R*distance;vec2 uv=screenUV(point);
        if(submerged(uv,surface,V)) {
            vec3 p=texture(scenePosition,uv).xyz;
            if((view*vec4(p-point,0)).z>=0.0) {
                float low=previous,high=distance;
                for(int refine=0;refine<5;++refine) {
                    float middle=(low+high)*.5;vec3 q=surface+R*middle;vec2 trial=screenUV(q);
                    bool behind=false;
                    if(submerged(trial,surface,V))behind=(view*vec4(texture(scenePosition,trial).xyz-q,0)).z>=0.0;
                    if(behind)high=middle;else low=middle;
                }
                hitUV=screenUV(surface+R*high);
                return submerged(hitUV,surface,V);
            }
        }
        previous=distance;
    }
    return false;
}
vec3 transmittedWater(vec3 N,vec3 V,vec3 L) {
    vec2 baseUV=screenUV(FragPos),uv=baseUV;
    float distance=deepWaterDistance;vec3 background=vec3(0);
    if(enableRefraction!=0 && submerged(baseUV,FragPos,V)) {
        vec3 p=texture(scenePosition,baseUV).xyz;
        // Snell direction with water IOR=1.333; strength blends toward straight transmission.
        vec3 R=normalize(mix(-V,refract(-V,N,1.0/1.333),refractionStrength));
        float rayLength=max(FragPos.y-p.y,0.0)/max(-R.y,.1);
        vec2 candidate;
        if(refractionStrength>0.0 && refractedUV(FragPos,V,R,min(deepWaterDistance,max(2.0*rayLength,1.0)),candidate))uv=candidate;
        distance=min(length(texture(scenePosition,uv).xyz-FragPos),deepWaterDistance);
        background=texture(opaqueScene,uv).rgb;
    }
    vec3 sigmaT=max(absorption+scattering,vec3(0));
    vec3 T=exp(-sigmaT*distance);
    vec3 albedo=scattering/max(sigmaT,vec3(1e-5));
    float g=scatteringAnisotropy,cosTheta=dot(-L,V);
    float phase=(1.0-g*g)/(4.0*PI*pow(max(1.0+g*g-2.0*g*cosTheta,1e-4),1.5));
    // Wave-top thickness is a visual estimate, not a traced volume thickness.
    float crest=clamp(max(FragPos.y-seaLevel,0.0)/waveHeightScale,0.0,1.0);
    float sunDepth=mix(2.0,.25,crest)/max(L.y,.2);
    vec3 sunT=exp(-sigmaT*sunDepth);
    vec3 tint=mix(outer_OceanColorShallow,outer_OceanColorDeep,1.0-exp(-distance*.08));
    vec3 source=tint*(skyRadiance(N)*.25+dirLight.diffuse*phase*sunT);
    vec3 scatter=subsurfaceStrength*albedo*(vec3(1)-T)*source;
    return background*T+scatter+outer_ambient;
}
void main() {
    vec3 N=normalize(texture(NormalRT,FragTexCoord).xyz),V=normalize(viewPos-FragPos);
    if(enableDetail!=0) {
        vec3 detail=normalize(texture(detailNormal,DetailTexCoord).xyz);
        vec2 slope=N.xz/max(N.y,.1)+detail.xz/max(detail.y,.1);
        N=normalize(vec3(slope.x,1,slope.y));
    }
    if(dot(N,V)<0.0)N=-N;
    vec3 L=normalize(-dirLight.direction);
    float NoV=max(dot(N,V),0.0),NoL=max(dot(N,L),0.0);
    float F0=clamp(outer_FresnelScale,0.0,1.0);
    float fresnel=F0+(1.0-F0)*pow(1.0-NoV,5.0);
    float foam=clamp(texture(BubblesRT,FragTexCoord).r,0.0,1.0);
    if(enableDetail!=0)foam=1.0-(1.0-foam)*(1.0-clamp(texture(detailFoam,DetailTexCoord).r,0.0,1.0));
    vec3 body=transmittedWater(N,V,L);
    vec3 reflection=skyRadiance(reflect(-V,N));
    // Normalized GGX sun highlight; existing gloss UI maps to roughness.
    float roughness=clamp(pow(2.0/float(max(outer_Gloss,1)+2),.25),.06,1.0);
    // Screen-space normal variance broadens unresolved highlights instead of letting them sparkle.
    float variance=dot(dFdx(N),dFdx(N))+dot(dFdy(N),dFdy(N));
    float alpha2=clamp(pow(roughness,4.0)+.5*variance,pow(.06,4.0),1.0);
    roughness=pow(alpha2,.25);
    vec3 sum=L+V;vec3 H=dot(sum,sum)>1e-8?normalize(sum):N;
    float NoH=max(dot(N,H),0.0),VoH=max(dot(V,H),0.0);
    float denom=NoH*NoH*(alpha2-1.0)+1.0;
    float D=alpha2/(PI*denom*denom);
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    float G=(NoV/(NoV*(1.0-k)+k))*(NoL/(NoL*(1.0-k)+k));
    float F=F0+(1.0-F0)*pow(1.0-VoH,5.0);
    vec3 sun=dirLight.specular*outer_Specular*(D*G*F/max(4.0*NoV*NoL,1e-5))*NoL;
    vec3 foamDiffuse=outer_BubblesColor*(dirLight.diffuse*NoL/PI+skyRadiance(N)*0.25);
    vec3 result=mix((1.0-fresnel)*body+fresnel*reflection+sun,foamDiffuse,foam);
    TemporalMotion=vec4(0);
    if(temporalActive!=0 && PreviousClip.w>0.0)TemporalMotion=vec4(PreviousClip.xy/PreviousClip.w*vec2(.5,-.5)+.5-screenUV(FragPos),PreviousDepth,1);
    FragColor=vec4(max(result,vec3(0)),1); // Tone mapping belongs to the HDR post pass.
}
