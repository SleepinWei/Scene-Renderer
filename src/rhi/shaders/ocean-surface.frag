#version 450
#extension GL_GOOGLE_include_directive : require
#include "sky-mapping.glsl"
const float PI=3.14159265359;
layout(set=0,binding=1,std140) uniform WaterFragment {mat4 waterFragmentVP;mat4 view;vec4 camera;vec4 direction;vec4 diffuse;vec4 specular;vec4 shallow;vec4 deep;vec4 foamColor;vec4 specularColor;vec4 ambientColor;vec4 optics;vec4 volume;vec4 absorb;vec4 scatter;vec4 surface;vec4 flags;mat4 coastInverse;mat4 coastModel;vec4 coastPatch;vec4 coastPreviousPatch;vec4 coastFeatures;vec4 coastBedInfo;vec4 meshBoundary;vec4 underwaterControls;};
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
layout(location=2) in vec2 WaterMaskCoord;
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
layout(set=2,binding=0) uniform sampler2D waterMask;
layout(set=2,binding=1) uniform sampler2D volumeDisplace;
layout(set=2,binding=2) uniform sampler2D bathymetryMap;
layout(set=2,binding=5) uniform sampler2D fragmentShoreState;
layout(set=2,binding=6) uniform sampler2D shoreFoam;
layout(set=2,binding=7) uniform sampler2D multipleScatterLut;
#include "water-waves.glsl"
#include "water-coast.glsl"
#include "water-shadow.glsl"
float lastWaterDistance,lastWaterSource,lastWaterConfidence;bool lastWaterHit;vec3 lastWaterT;
bool queryAboveWater=false;
vec2 waterQueryUV(vec2 uv) {
    return volume.w>.5?vec2(uv.x*.5+(queryAboveWater?.5:0.),uv.y):uv;
}
ivec2 waterQuerySize() {
    ivec2 size=textureSize(scenePosition,0);if(volume.w>.5)size.x/=2;return size;
}
vec4 waterQueryPosition(vec2 uv) {
    return texture(scenePosition,waterQueryUV(uv));
}
vec4 waterQueryTexel(ivec2 pixel) {
    if(queryAboveWater&&volume.w>.5)pixel.x+=waterQuerySize().x;
    return texelFetch(scenePosition,pixel,0);
}
vec3 waterQueryColor(vec2 uv) {
    return texture(opaqueScene,waterQueryUV(uv)).rgb;
}

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
    vec4 samplePosition=waterQueryPosition(uv);vec3 p=samplePosition.xyz;
    if(!(volume.w>0.5 ? samplePosition.w>.5 : dot(texture(sceneNormal,uv).xyz,texture(sceneNormal,uv).xyz)>.25))return false;
    if(volume.w>.5)return true; // Both atlas layers were clipped by the detailed wave surface.
    float macro=waterMacroHeight(p.xz,volumeDisplace,vec4(seaLevel,surface.z,32,0));
    float height=coastHeight(p.xz,macro,fragmentShoreState,coastPatch,seaLevel,coastFeatures.z);
    return queryAboveWater?p.y>=height-.002:p.y<height-.002;
}
// Intersect the refracted ray with the visible opaque depth layer. A single
// depth-projected offset duplicates silhouettes when a sphere replaces the floor.
bool refractedUV(vec3 surface,vec3 V,vec3 R,float range,out vec2 hitUV) {
    float previous=0.0;
    for(int step=1;step<=24;++step) {
        float distance=range*float(step)/24.0;
        vec3 point=surface+R*distance;vec2 uv=screenUV(point);
        if(submerged(uv,surface,V)) {
            vec3 p=waterQueryPosition(uv).xyz;
            if((view*vec4(p-point,0)).z>=0.0) {
                float low=previous,high=distance;
                for(int refine=0;refine<5;++refine) {
                    float middle=(low+high)*.5;vec3 q=surface+R*middle;vec2 trial=screenUV(q);
                    bool behind=false;
                    if(submerged(trial,surface,V))behind=(view*vec4(waterQueryPosition(trial).xyz-q,0)).z>=0.0;
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
#include "water-refraction.glsl"
#define WATER_MEDIUM_VISIBILITY(point) waterSunVisibility(point,int(scatter.w))
#include "water-medium.glsl"
vec3 underwaterSegment(vec3 background,vec3 origin,vec3 ray,float distance,vec3 L) {
    if(underwaterControls.y<.5)return background;
    return waterMediumTransport(background,origin,ray,distance,L,dirLight.diffuse,skyRadiance(vec3(0,1,0)),
        absorption,scattering,scatteringAnisotropy,volume.z>0.?subsurfaceStrength:0.,volumeDisplace,fragmentShoreState,vec4(seaLevel,surface.z,32,0));
}
vec3 bedRadiance(vec3 point,vec3 base,vec3 bedN,vec3 L) {
    vec3 towardSun=-refract(-L,vec3(0,1,0),1./1.333);
    if(coastFeatures.z>.5&&coastBedInfo.y>.5){vec2 at=(point.xz-coastPatch.xy)/coastPatch.z;
        if(all(greaterThanEqual(at,vec2(0)))&&all(lessThanEqual(at,vec2(1))))base*=mix(1.,.45,texture(shoreFoam,at).g);}
    float depth=max(waterMediumHeight(point.xz,volumeDisplace,fragmentShoreState,vec4(seaLevel,surface.z,32,0))-point.y,0.);
    float sunF=.02037+.97963*pow(1.-max(L.y,0.),5.);
    vec3 light=dirLight.diffuse*max(dot(bedN,towardSun),0.)*(1.-sunF)*exp(-(absorption+scattering)*depth/max(towardSun.y,.05))*waterSunVisibility(point,int(scatter.w));
    return base*(light/PI+skyRadiance(vec3(0,1,0))*.25);
}
vec3 underwaterInterface(vec3 N,vec3 V,vec3 L,out float F) {
    F=waterDielectricFresnel(max(dot(N,V),0.),1.333);
    float eyeDistance=min(length(viewPos-FragPos),deepWaterDistance);
    lastWaterDistance=eyeDistance;lastWaterT=underwaterControls.y>.5?exp(-(absorption+scattering)*eyeDistance):vec3(1);
    lastWaterSource=0.;lastWaterConfidence=1.;lastWaterHit=false;
    vec3 transmitted=vec3(0),reflected=vec3(0);vec2 hitUV;float distance,confidence;
    vec3 R=refract(-V,N,1.333);
    if(F<1.&&underwaterControls.z>.5&&dot(R,R)>1e-8){
        R=normalize(mix(-V,normalize(R),refractionStrength));transmitted=skyRadiance(R);
        queryAboveWater=true;
        if(camera.w>.5&&waterDDA(FragPos,V,R,max(underwaterControls.w,deepWaterDistance),hitUV,distance,confidence)){
            transmitted=waterQueryColor(hitUV);lastWaterSource=1.;lastWaterConfidence=confidence;lastWaterHit=true;}
    }
    queryAboveWater=false;R=normalize(reflect(-V,N));float reflectedDistance=deepWaterDistance;
    bool bedHit=false;
    if(coastBedInfo.x>.5){vec3 base,bedN;
        if(waterBedRay(FragPos,R,deepWaterDistance,distance,base,bedN)){
            vec3 point=FragPos+R*distance;
            reflected=bedRadiance(point,base,bedN,L);reflectedDistance=distance;bedHit=true;}
    }
    // Use one stable heightfield shading model for the floor on both sides of
    // the screen boundary. Screen capture still supplies objects in front of
    // that floor; otherwise its capture/fallback lighting seam is conspicuous.
    if(camera.w>.5&&waterDDA(FragPos,V,R,deepWaterDistance,hitUV,distance,confidence)&&
       (!bedHit||((volume.w>.5?waterQueryPosition(hitUV).w<1.5:distance<reflectedDistance-.08)&&distance<=reflectedDistance+.08))){
        reflected=waterQueryColor(hitUV);reflectedDistance=distance;
    }
    reflected=underwaterSegment(reflected,FragPos,R,reflectedDistance,L);
    return mix(transmitted,reflected,F);
}
float foamNoise(vec2 p) {
    vec2 cell=floor(p),f=fract(p);f=f*f*(3.-2.*f);
    vec4 hash=fract(sin(vec4(dot(cell,vec2(127.1,311.7)),dot(cell+vec2(1,0),vec2(127.1,311.7)),dot(cell+vec2(0,1),vec2(127.1,311.7)),dot(cell+1.,vec2(127.1,311.7))))*43758.5453);
    return mix(mix(hash.x,hash.y,f.x),mix(hash.z,hash.w,f.x),f.y);
}
vec3 transmittedWater(vec3 N,vec3 V,vec3 L) {
    vec2 baseUV=screenUV(FragPos),uv=baseUV;
    float distance=deepWaterDistance;vec3 background=vec3(0);
    vec3 R=normalize(mix(-V,refract(-V,N,1.0/1.333),refractionStrength));
    bool baseValid=submerged(baseUV,FragPos,V),hit=false;lastWaterSource=0.;lastWaterConfidence=0.;
    if(enableRefraction!=0){
        float range=deepWaterDistance;
        if(baseValid){vec3 p=waterQueryPosition(baseUV).xyz;
            float rayLength=max(FragPos.y-p.y,0.0)/max(-R.y,.1);
            range=min(range,max(2.0*rayLength,1.0));}
        vec2 candidate;
        // Search even when the straight camera ray has no underwater sample.
        if(coastFeatures.x>.5) {
            float d,confidence;
            if(camera.w>.5&&waterDDA(FragPos,V,R,range,candidate,d,confidence)){
                uv=candidate;hit=true;distance=d;background=waterQueryColor(uv);lastWaterSource=1.;lastWaterConfidence=confidence;
                vec4 samplePosition=waterQueryPosition(uv);float bed;vec3 base;
                if(volume.w>.5&&samplePosition.w>1.5&&waterBed(samplePosition.xz,bed,base))
                    background=bedRadiance(samplePosition.xyz,base,waterBedNormal(samplePosition.xz,bed),L);
            }
            if(!hit&&coastBedInfo.x>.5){vec3 base,bedN;
                if(waterBedRay(FragPos,R,deepWaterDistance,d,base,bedN)) {
                    background=bedRadiance(FragPos+R*d,base,bedN,L);distance=d;hit=true;lastWaterSource=2.;lastWaterConfidence=1.;
                }
            }
        } else {
            if(refractionStrength>0.0&&refractedUV(FragPos,V,R,range,candidate)){uv=candidate;hit=true;}
            else hit=baseValid;
            if(hit){distance=min(length(waterQueryPosition(uv).xyz-FragPos),deepWaterDistance);background=waterQueryColor(uv);lastWaterSource=1.;lastWaterConfidence=1.;}
        }
    }
    lastWaterDistance=distance;lastWaterHit=hit;
    vec3 sigmaT=max(absorption+scattering,vec3(0));
    vec3 T=exp(-sigmaT*distance);lastWaterT=T;
    vec3 albedo=scattering/max(sigmaT,vec3(1e-5));
    float g=scatteringAnisotropy;
    if(volume.z>0&&subsurfaceStrength>0&&dot(scattering,scattering)>0){
        vec4 waves=vec4(seaLevel,surface.z,32,surface.w);
        vec3 towardSun=-refract(-L,N,1./1.333);
        if(dot(towardSun,towardSun)<1e-8)towardSun=vec3(0,1,0);
        towardSun=normalize(towardSun);
        float cosine=dot(towardSun,R);
        float phase=(1.-g*g)/(4.*PI*pow(max(1.+g*g-2.*g*cosine,1e-4),1.5));
        float sunF=.02037+.97963*pow(1.-max(dot(N,L),0.),5.);
        vec3 ambientSource=skyRadiance(vec3(0,1,0))*.25;
        vec3 accumulated=vec3(0);float stepLength=distance/volume.z;
        vec3 segmentWeight=albedo*(1.-exp(-sigmaT*stepLength));
        for(int i=0;i<4;++i){
            float t=(float(i)+.5)*stepLength;vec3 point=FragPos+R*t;
            float skyDistance;
            float sunlightDistance=waterVolumeSunDistance(point,towardSun,volumeDisplace,waves,deepWaterDistance,skyDistance);
            if(coastFeatures.z>.5){float h=coastHeight(point.xz,waterMacroHeight(point.xz,volumeDisplace,waves),fragmentShoreState,coastPatch,seaLevel,1.);
                skyDistance=max(h-point.y,0.);sunlightDistance=skyDistance/max(towardSun.y,.05);}
            vec3 source=ambientSource*exp(-sigmaT*skyDistance*1.5);
            if(L.y>0)source+=dirLight.diffuse*phase*(1.-sunF)*exp(-sigmaT*sunlightDistance)*waterSunVisibility(point,int(scatter.w));
            accumulated+=exp(-sigmaT*(float(i)*stepLength))*segmentWeight*source;
        }
        if(coastFeatures.y>.5) {
            float h=max(distance*max(-R.y,.05),.001),bed;vec3 ignored;
            if(waterBed(FragPos.xz,bed,ignored))h=max(FragPos.y-bed,.001);
            vec3 response;
            for(int c=0;c<3;++c)response[c]=waterSlabLut(sigmaT[c]*h,albedo[c],g,max(L.y,.1));
            vec3 incoming=skyRadiance(vec3(0,1,0))*PI*.25;
            if(L.y>0.)incoming+=dirLight.diffuse*L.y*(1.-sunF)*waterSunVisibility(FragPos,int(scatter.w));
            // The LUT contains only 2+ scatter flux; isotropic angular closure.
            accumulated+=incoming*response/PI;
        }
        return background*T+subsurfaceStrength*accumulated+outer_ambient;
    }
    if(volume.z>0)return background*T+outer_ambient;
    float cosTheta=dot(-L,V);
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
    // Mask follows material UVs, whose V axis is opposite the water grid.
    bool maskWet=texture(waterMask,vec2(WaterMaskCoord.x,1.-WaterMaskCoord.y)).r>=.5;
    vec4 localShore=vec4(0);float shoreWeight=0.;
    if(coastFeatures.z>.5){localShore=coastSample(fragmentShoreState,FragPos.xz,coastPatch);shoreWeight=coastBlend(FragPos.xz,localShore,coastPatch,seaLevel);}
    if(!maskWet&&shoreWeight<.01)discard;
    float bedHeight;vec3 bedColor;
    if(coastBedInfo.x>.5&&coastFeatures.z>.5&&waterBed(FragPos.xz,bedHeight,bedColor)&&FragPos.y-bedHeight<.006)discard;
    vec2 largeTexels=FragTexCoord*vec2(textureSize(NormalRT,0));
    float largeFootprint=max(length(dFdx(largeTexels)),length(dFdy(largeTexels)));
    // The live FFT textures have one mip. Fade unresolved slope toward its
    // up-vector mean to avoid a sparkling/broken band at the far mesh edge.
    float largeResolved=1.-smoothstep(8.,64.,largeFootprint);
    vec3 N=normalize(mix(vec3(0,1,0),texture(NormalRT,FragTexCoord).xyz,largeResolved)),V=normalize(viewPos-FragPos);vec2 fineSlope=vec2(0);
    if(enableDetail!=0) {
        vec3 detail=normalize(texture(detailNormal,DetailTexCoord).xyz);
        vec2 detailTexels=DetailTexCoord*vec2(textureSize(detailNormal,0));
        float detailResolved=1.-smoothstep(2.,16.,max(length(dFdx(detailTexels)),length(dFdy(detailTexels))));
        fineSlope=detail.xz/max(detail.y,.1)*detailResolved;
        vec2 slope=N.xz/max(N.y,.1)+fineSlope;
        N=normalize(vec3(slope.x,1,slope.y));
    }
    if(shoreWeight>0.) {
        vec2 dx=vec2(coastPatch.w,0);
        float a=coastElevation(fragmentShoreState,FragPos.xz-dx,coastPatch,seaLevel),b=coastElevation(fragmentShoreState,FragPos.xz+dx,coastPatch,seaLevel);
        float c=coastElevation(fragmentShoreState,FragPos.xz-dx.yx,coastPatch,seaLevel),d=coastElevation(fragmentShoreState,FragPos.xz+dx.yx,coastPatch,seaLevel);
        vec3 shallowN=normalize(vec3(-(b-a)/(2.*coastPatch.w),1,-(d-c)/(2.*coastPatch.w)));
        shallowN=normalize(vec3(shallowN.xz/max(shallowN.y,.1)+fineSlope*smoothstep(.01,.4,localShore.x),1).xzy);
        N=normalize(mix(N,shallowN,shoreWeight));
    }
    if(dot(N,V)<0.0)N=-N;
    vec3 L=normalize(-dirLight.direction);
    float NoV=max(dot(N,V),0.0),NoL=max(dot(N,L),0.0);
    float F0=clamp(outer_FresnelScale,0.0,1.0);
    float fresnel=F0+(1.0-F0)*pow(1.0-NoV,5.0);
    float foam=clamp(texture(BubblesRT,FragTexCoord).r,0.0,1.0)*largeResolved;
    if(enableDetail!=0){vec2 at=DetailTexCoord*vec2(textureSize(detailFoam,0));float resolved=1.-smoothstep(2.,16.,max(length(dFdx(at)),length(dFdy(at))));
        foam=1.0-(1.0-foam)*(1.0-clamp(texture(detailFoam,DetailTexCoord).r,0.0,1.0)*resolved);}
    if(shoreWeight>0.&&coastFeatures.w>.5){float coastalFoam=texture(shoreFoam,(FragPos.xz-coastPatch.xy)/coastPatch.z).r;
        vec2 velocity=localShore.yz/max(localShore.x,.02),foamUV=FragPos.xz-velocity*.15;
        float pattern=.65*foamNoise(foamUV*2.)+.35*foamNoise(foamUV*9.);
        float footprint=max(length(dFdx(foamUV)),length(dFdy(foamUV)));
        float broken=coastalFoam*smoothstep(.15,.8,coastalFoam-pattern*.55+.25);
        coastalFoam=mix(broken,coastalFoam*.8,smoothstep(.1,.6,footprint));
        foam=1.-(1.-foam)*(1.-coastalFoam*shoreWeight);}
    vec3 refractionN=N;
    if(coastFeatures.x>.5){float variation=dot(dFdx(N),dFdx(N))+dot(dFdy(N),dFdy(N));
        vec3 macro=normalize(texture(NormalRT,FragTexCoord).xyz);if(dot(macro,V)<0.)macro=-macro;
        refractionN=normalize(mix(N,macro,clamp(variation*16.,0.,.75)*(1.-shoreWeight)));}
    if(underwaterControls.x>.5&&waterEyeInside(viewPos,volumeDisplace,fragmentShoreState,waterMask,vec4(seaLevel,surface.z,32,0),meshBoundary.x)){
        // Normal points into the incident medium. A local wave slope may face
        // away even though the eye is below the macro surface.
        if(dot(refractionN,V)<0.)refractionN=-refractionN;
        float interfaceF;vec3 result=underwaterInterface(refractionN,V,L,interfaceF);
        vec3 foamLight=outer_BubblesColor*(dirLight.diffuse*max(L.y,0.)/PI+skyRadiance(vec3(0,1,0))*.25);
        result=mix(result,foamLight,foam);
        result=underwaterSegment(result,viewPos,normalize(FragPos-viewPos),lastWaterDistance,L);
        if(absorb.w>0.)result=absorb.w<1.5?lastWaterT:absorb.w<2.5?vec3(lastWaterDistance/deepWaterDistance):absorb.w<3.5?vec3(lastWaterHit?1.:0.):absorb.w<4.5?vec3(lastWaterSource==1.?1.:0.,0,lastWaterSource==0.?1.:0.):vec3(lastWaterConfidence);
        // Screen-space hits and rapidly changing critical-angle boundaries are
        // unreliable optical histories; keep their temporal confidence zero.
        TemporalMotion=vec4(0);FragColor=vec4(max(result,vec3(0)),1);return;
    }
    vec3 body=transmittedWater(refractionN,V,L);
    // Explicit diagnostics use linear HDR; they do not alter optical parameters.
    if(absorb.w>0){vec3 diagnostic=absorb.w<1.5?lastWaterT:absorb.w<2.5?vec3(lastWaterDistance/deepWaterDistance):absorb.w<3.5?vec3(lastWaterHit?1.:0.):absorb.w<4.5?vec3(lastWaterSource==1.?1.:0.,lastWaterSource==2.?1.:0.,lastWaterSource==0.?1.:0.):vec3(lastWaterConfidence);
        FragColor=vec4(diagnostic,1);TemporalMotion=vec4(0);return;}

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
    // Blend the finite kilometre-scale mesh border into the sky on the same
    // viewing ray. Near-shore transport is unaffected; this is a display-domain
    // seam treatment, not an atmospheric scattering integral.
    float boundaryFade=meshBoundary.x>=512. && hasSky!=0?smoothstep(.8,.995,max(abs(FragPos.x),abs(FragPos.z))/meshBoundary.x):0.;
    result=mix(result,skyRadiance(-V),boundaryFade);
    TemporalMotion=vec4(0);
    if(temporalActive!=0 && PreviousClip.w>0.0)TemporalMotion=vec4(PreviousClip.xy/PreviousClip.w*vec2(.5,-.5)+.5-screenUV(FragPos),PreviousDepth,1);
    if(coastFeatures.x>.5)TemporalMotion.w*=lastWaterConfidence;
    TemporalMotion.w*=1.-boundaryFade;
    FragColor=vec4(max(result,vec3(0)),1); // Tone mapping belongs to the HDR post pass.
}
