#include "sky-mapping.glsl"
#include "water-surface-model.glsl"
const float PI=3.14159265359;
layout(set=0,binding=1,std140) uniform WaterFragment {mat4 waterFragmentVP;mat4 view;vec4 camera;vec4 direction;vec4 diffuse;vec4 specular;vec4 shallow;vec4 deep;vec4 foamColor;vec4 specularColor;vec4 ambientColor;vec4 optics;vec4 volume;vec4 absorb;vec4 scatter;vec4 surface;vec4 flags;mat4 coastInverse;mat4 coastModel;vec4 coastPatch;vec4 coastPreviousPatch;vec4 coastFeatures;vec4 coastBedInfo;vec4 meshBoundary;vec4 underwaterControls;vec4 causticPatch;vec4 causticControls;vec4 diving;vec4 causticMiddle;vec4 causticFar;vec4 causticProjection;vec4 shaftPatch;vec4 shaftProjection;mat4 airVP;mat4 airView;vec4 airCapture;vec4 sunDisplay;mat4 reflectionVP;mat4 reflectionView;vec4 volumeViewport;};
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
#define WATER_SLOPE_FRAGMENT
#include "water-slope-filter.glsl"
#if WATER_FILTERED_LAYOUT
layout(set=1,binding=0) uniform sampler2DArray NormalRT;
layout(set=1,binding=3) uniform sampler2DArray detailNormal;
vec4 waterLargeNormal(vec2 uv){return textureLod(NormalRT,vec3(uv,2),0.);}
vec4 waterDetailNormal(vec2 uv){return textureLod(detailNormal,vec3(uv,2),0.);}
#else
layout(set=1,binding=0) uniform sampler2D NormalRT;
layout(set=1,binding=3) uniform sampler2D detailNormal;
vec4 waterLargeNormal(vec2 uv){return texture(NormalRT,uv);}
vec4 waterDetailNormal(vec2 uv){return texture(detailNormal,uv);}
#endif
layout(set=1,binding=1) uniform sampler2D sunShaftField;
layout(set=1,binding=2) uniform sampler2D skyview;
layout(set=1,binding=4) uniform sampler2D bedCausticMap;
layout(set=1,binding=5) uniform sampler2D opaqueScene;
layout(set=1,binding=6) uniform sampler2D scenePosition;
layout(set=1,binding=7) uniform sampler2D sceneNormal;
layout(set=2,binding=0) uniform sampler2D waterMask;
layout(set=2,binding=1) uniform sampler2D volumeDisplace;
layout(set=2,binding=2) uniform sampler2D bathymetryMap;
layout(set=2,binding=5) uniform sampler2D fragmentShoreState;
layout(set=2,binding=6) uniform sampler2D shoreFoam;
layout(set=2,binding=7) uniform sampler2DArray multipleScatterLut;

#include "water-caustics-sample.glsl"
#include "water-waves.glsl"
#include "water-coast.glsl"
#include "water-shadow.glsl"
float lastWaterDistance,lastWaterSource,lastWaterConfidence;bool lastWaterHit;vec3 lastWaterT;
bool queryAboveWater=false,queryWideAir=false,queryReflection=false;
float waterQueryColumn(){return queryReflection?airCapture.z:queryWideAir?2.:queryAboveWater?1.:0.;}
mat4 waterQueryVP(){return queryReflection?reflectionVP:queryWideAir?airVP:waterFragmentVP;}
mat4 waterQueryView(){return queryReflection?reflectionView:queryWideAir?airView:view;}
vec2 waterQueryUV(vec2 uv) {
    return volume.w>.5?vec2((uv.x+waterQueryColumn())/airCapture.y,uv.y):uv;
}
ivec2 waterQuerySize() {
    ivec2 size=textureSize(scenePosition,0);if(volume.w>.5)size.x/=int(airCapture.y);return size;
}
vec4 waterQueryPosition(vec2 uv) {
    return texture(scenePosition,waterQueryUV(uv));
}
vec4 waterQueryTexel(ivec2 pixel) {
    if(queryAboveWater&&volume.w>.5)pixel.x+=waterQuerySize().x*int(waterQueryColumn());
    return texelFetch(scenePosition,pixel,0);
}
vec3 waterQueryColor(vec2 uv) {
    return texture(opaqueScene,waterQueryUV(uv)).rgb;
}

vec3 skyRadiance(vec3 direction) {
    if(hasSky==0)return vec3(0);
    return sampleSkyLut(skyview,direction);
}
vec3 waterAirSky(vec3 ray,vec3 towardSun){
    vec3 radiance=skyRadiance(ray);
    if(hasSky!=0&&towardSun.y>0.&&ray.y>0.){
        float angle=atan(length(cross(ray,towardSun)),clamp(dot(ray,towardSun),-1.,1.));
        float footprint=max(fwidth(angle),1e-6),disk=1.-smoothstep(sunDisplay.w-footprint*.5,sunDisplay.w+footprint*.5,angle);
        radiance+=sunDisplay.rgb*disk;
    }
    return min(radiance,vec3(65000));
}
vec2 screenUV(vec3 point) {
    vec4 clip=waterFragmentVP*vec4(point,1);
    return clip.xy/max(clip.w,1e-5)*vec2(.5,-.5)+.5;
}
vec2 waterQueryScreenUV(vec3 point){vec4 clip=waterQueryVP()*vec4(point,1);return clip.xy/max(clip.w,1e-5)*vec2(.5,-.5)+.5;}
bool submerged(vec2 uv,vec3 surface,vec3 V) {
    if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))return false;
    vec4 samplePosition=waterQueryPosition(uv);vec3 p=samplePosition.xyz;
    if(!((volume.w>0.5||coastBedInfo.w>.5) ? samplePosition.w>.5 : dot(texture(sceneNormal,uv).xyz,texture(sceneNormal,uv).xyz)>.25))return false;
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
#include "water-shafts-sample.glsl"
#define WATER_MEDIUM_SOLAR_FLUX(point) waterSunShaftFlux(point,sunShaftField,shaftPatch,shaftProjection,diving.z)
#define WATER_MEDIUM_STEPS int(diving.w)
#define WATER_PARTICLE_DENSITY (meshBoundary.z>.5?0.:diving.y)
#define WATER_PARTICLE_TIME diving.x
#include "water-medium.glsl"
bool volumeCacheAccepted=false;vec3 volumeCachedScatter=vec3(0);
float volumeReflectedDistance=0.;int volumeReflectedKind=0;
vec2 volumeEncodeNormal(vec3 n){n/=abs(n.x)+abs(n.y)+abs(n.z);return n.z>=0.?n.xy:(1.-abs(n.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(n.xy,vec2(0)));}
vec3 volumeDecodeNormal(vec2 p){vec3 n=vec3(p,1.-abs(p.x)-abs(p.y));float t=max(-n.z,0.);n.xy+=mix(vec2(t),vec2(-t),greaterThanEqual(n.xy,vec2(0)));return normalize(n);}
bool volumeLookup(vec3 N,float eyeDistance,float reflectedDistance,float F,int kind){
    if(meshBoundary.y<.5||meshBoundary.z>.5)return false;
    ivec2 size=ivec2(volumeViewport.xy);vec2 coordinate=screenUV(FragPos)*vec2(size)-.5,f=fract(coordinate);ivec2 first=ivec2(floor(coordinate));
    vec3 sum=vec3(0);float total=0.;
    for(int i=0;i<4;++i){
        ivec2 at=clamp(first+ivec2(i%2,i/2),ivec2(0),size-1);
        vec4 value=texelFetch(multipleScatterLut,ivec3(at,0),0),guide=texelFetch(multipleScatterLut,ivec3(at,1),0);
        int hit=int(floor(guide.w/4.));float oldF=guide.w-float(hit)*4.;
        if(value.w<=0.||hit!=kind||(F>=.999)!=(oldF>=.999)||abs(F-oldF)>.04)continue;
        float nd=dot(N,volumeDecodeNormal(guide.xy));
        if(nd<.995||abs(value.w-eyeDistance)>.03+eyeDistance*.04||abs(guide.z-reflectedDistance)>.05+reflectedDistance*.04)continue;
        float weight=(i%2==0?1.-f.x:f.x)*(i/2==0?1.-f.y:f.y);
        sum+=value.rgb*weight;total+=weight;
    }
    if(total<.45)return false;volumeCachedScatter=sum/total;return true;
}
vec3 underwaterSegment(vec3 background,vec3 origin,vec3 ray,float distance,vec3 L,bool reflected) {
    if(underwaterControls.y<.5)return background;
    vec4 waves=vec4(seaLevel,surface.z,32,0);vec3 sigma=max(absorption+scattering,vec3(0));
    // The half-resolution pass computes only S_eye + T_eye*F*S_ref. Its
    // background is zero, while the eye leg still transports S_ref correctly.
    if(meshBoundary.z>.5&&reflected)background=vec3(0);
    if(volumeCacheAccepted){
        vec3 particles=waterMediumParticles(origin,ray,distance,L,dirLight.diffuse,skyRadiance(vec3(0,1,0)),sigma,volumeDisplace,fragmentShoreState,waves);
        return background*exp(-sigma*distance)+particles;
    }
    return waterMediumTransport(background,origin,ray,distance,L,dirLight.diffuse,skyRadiance(vec3(0,1,0)),
        absorption,scattering,scatteringAnisotropy,volume.z>0.?subsurfaceStrength:0.,volumeDisplace,fragmentShoreState,waves);
}
vec3 bedRadiance(vec3 point,vec3 base,vec3 bedN,vec3 L) {
    vec3 towardSun=-refract(-L,vec3(0,1,0),1./1.333);
    if(coastFeatures.z>.5&&coastBedInfo.y>.5){vec2 at=(point.xz-coastPatch.xy)/coastPatch.z;
        if(all(greaterThanEqual(at,vec2(0)))&&all(lessThanEqual(at,vec2(1))))base*=mix(1.,.45,texture(shoreFoam,at).g);}
    float depth=max(waterMediumHeight(point.xz,volumeDisplace,fragmentShoreState,vec4(seaLevel,surface.z,32,0))-point.y,0.);
    float sunF=.02037+.97963*pow(1.-max(L.y,0.),5.);
    vec3 light=dirLight.diffuse*max(dot(bedN,towardSun),0.)*(1.-sunF)*exp(-(absorption+scattering)*depth/max(towardSun.y,.05))*waterSunVisibility(point,int(scatter.w))*waterCausticFactor(point,bedCausticMap,causticPatch,causticMiddle,causticFar,causticProjection,causticControls);
    return base*(light/PI+skyRadiance(vec3(0,1,0))*.25);
}
vec3 underwaterInterface(vec3 N,vec3 V,vec3 L,out float F) {
    F=waterDielectricFresnel(max(dot(N,V),0.),1.333);
    float eyeDistance=min(length(viewPos-FragPos),deepWaterDistance);
    lastWaterDistance=eyeDistance;lastWaterT=underwaterControls.y>.5?exp(-(absorption+scattering)*eyeDistance):vec3(1);
    lastWaterSource=0.;lastWaterConfidence=1.;lastWaterHit=false;
    vec3 transmitted=vec3(0),reflected=vec3(0);vec2 hitUV;float distance,confidence;
    vec3 R=refract(-V,N,1.333);
    if(meshBoundary.z<.5&&F<1.&&underwaterControls.z>.5&&dot(R,R)>1e-8){
        R=normalize(mix(-V,normalize(R),refractionStrength));transmitted=waterAirSky(R,L);
        queryAboveWater=true;
        if(camera.w>.5&&waterDDA(FragPos,V,R,max(underwaterControls.w,deepWaterDistance),hitUV,distance,confidence)){
            transmitted=waterQueryColor(hitUV);lastWaterSource=1.;lastWaterConfidence=confidence;lastWaterHit=true;}
        else if(camera.w>.5&&airCapture.w>.5){
            queryWideAir=true;
            if(waterDDA(FragPos,V,R,max(underwaterControls.w,deepWaterDistance),hitUV,distance,confidence)){
                transmitted=waterQueryColor(hitUV);lastWaterSource=3.;lastWaterConfidence=confidence;lastWaterHit=true;}
            queryWideAir=false;
        }
    }
    queryAboveWater=false;queryWideAir=false;R=normalize(reflect(-V,N));float reflectedDistance=deepWaterDistance;
    bool bedHit=false,reflectedObject=false;
    if(coastBedInfo.x>.5){vec3 base,bedN;
        if(waterBedRay(FragPos,R,deepWaterDistance,distance,base,bedN)){
            vec3 point=FragPos+R*distance;
            if(meshBoundary.z<.5)reflected=bedRadiance(point,base,bedN,L);reflectedDistance=distance;bedHit=true;}
    }
    // Use one stable heightfield shading model for the floor on both sides of
    // the screen boundary. Screen capture still supplies objects in front of
    // that floor; otherwise its capture/fallback lighting seam is conspicuous.
    if(camera.w>.5&&waterDDA(FragPos,V,R,bedHit?min(deepWaterDistance,reflectedDistance+.08):deepWaterDistance,hitUV,distance,confidence)&&
       (!bedHit||((volume.w>.5?waterQueryPosition(hitUV).w<1.5:distance<reflectedDistance-.08)&&distance<=reflectedDistance+.08))){
        if(meshBoundary.z<.5)reflected=waterQueryColor(hitUV);reflectedDistance=distance;reflectedObject=true;
    }
    volumeReflectedDistance=reflectedDistance;volumeReflectedKind=bedHit?1:0;
    // Independent opaque objects carry their own optical category in the guide.
    if(reflectedObject)volumeReflectedKind=2;
    volumeCacheAccepted=volumeLookup(N,eyeDistance,reflectedDistance,F,volumeReflectedKind);
    reflected=underwaterSegment(reflected,FragPos,R,reflectedDistance,L,true);
    if(meshBoundary.z>.5)transmitted=vec3(0);
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
            if(coastFeatures.z>.5){float h=coastHeight(point.xz,waterMacroHeight(point.xz,volumeDisplace,waves),fragmentShoreState,coastPatch,seaLevel,coastFeatures.z);
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
#if WATER_FILTERED_LAYOUT
vec3 waterSubpixelNormal(vec2 uv,vec2 detailUV,float largeResolved,float fineResolved){
    vec3 n=normalize(mix(vec3(0,1,0),waterLargeNormal(uv).xyz,largeResolved));
    if(enableDetail!=0){vec3 d=normalize(waterDetailNormal(detailUV).xyz);vec2 slope=n.xz/max(n.y,.1)+d.xz/max(d.y,.1)*fineResolved;n=normalize(vec3(slope.x,1,slope.y));}
    return n;
}
// Integrate resolved normals in the pixel rather than replacing their
// potentially multimodal reflection distribution with one Gaussian.
vec3 waterPixelSun(vec3 L,vec2 du,vec2 dv,vec2 fu,vec2 fv,vec3 px,vec3 py,float largeFootprint,float fineFootprint){
    if(L.y<=0.)return vec3(0);
    // A unit-area separable tent reconstructs narrow glints across pixel edges.
    // Integrate in linear radiance, before display mapping. The box ablation
    // retains the previous 4x4 footprint; no exposure / intensity compensation.
    bool tent=ambientColor.w>.5;int count=tent?(specularColor.w>=.02?4:8):4;
    float span=tent?2.:1.,step=span/float(count);
    float macro=1.-smoothstep(8.,64.,largeFootprint*step),fine=1.-smoothstep(2.,16.,fineFootprint*step);
    vec3 sum=vec3(0);float total=0.;
    for(int y=0;y<count;++y)for(int x=0;x<count;++x){
        vec2 o=(vec2(x,y)+.5)*step-span*.5,uv=FragTexCoord+du*o.x+dv*o.y,detailUV=DetailTexCoord+fu*o.x+fv*o.y;
        float weight=tent?(1.-abs(o.x))*(1.-abs(o.y)):1.;total+=weight;
        vec3 position=FragPos+px*o.x+py*o.y,V=normalize(viewPos-position);
        vec3 N=waterSubpixelNormal(uv,detailUV,macro,fine);if(dot(N,V)<0.)N=-N;
        if(specularColor.w>=.02){sum+=weight*waterRoughSun(N,V,L,dirLight.diffuse,sunDisplay.w,specularColor.w);continue;}
        vec3 R=reflect(-V,N),nx=waterSubpixelNormal(uv+du*step,detailUV+fu*step,macro,fine),ny=waterSubpixelNormal(uv+dv*step,detailUV+fv*step,macro,fine);
        vec3 vx=normalize(viewPos-position-px*step),vy=normalize(viewPos-position-py*step);
        vec3 rx=reflect(-vx,nx),ry=reflect(-vy,ny);
        float solar=waterSolarAngularKernel(R,L,sunDisplay.w,rx-R,ry-R);
        if(dot(N,L)>0.)sum+=weight*dirLight.diffuse*dielectricFresnel(max(dot(N,V),0.),1.,1.333)*solar;
    }
    return sum/total;
}
// Keep Fresnel and sky radiance correlated over the same normal distribution.
// Unresolved slopes use Gaussian quadrature; resolved waves use spatial samples.
// Rays below the sky remain below it: this filters, rather than inventing,
// radiance for missing secondary water reflections.
vec3 waterPixelSky(vec3 V,vec2 mean,mat2 covariance,vec2 du,vec2 dv,vec2 fu,vec2 fv,vec3 px,vec3 py,
                   float largeFootprint,float fineFootprint,out float meanF){
    float resolved=1.-smoothstep(4.,8.,max(largeFootprint,enableDetail!=0?fineFootprint:0.));
    vec4 nodes=vec4(-2.3344142,-.7419638,.7419638,2.3344142);
    vec4 weights=vec4(.04587585,.45412415,.45412415,.04587585);
    float a=sqrt(max(covariance[0][0],0.)),b=a>1e-8?covariance[0][1]/a:0.;
    float c=sqrt(max(covariance[1][1]-b*b,0.));
    vec3 distribution=vec3(0);float distributionF=0.;
    if(resolved<1.)for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        vec2 slope=mean+vec2(a*nodes[x],b*nodes[x]+c*nodes[y]);vec3 n=normalize(vec3(slope.x,1,slope.y));
        if(dot(n,V)<0.)n=-n;float F=dielectricFresnel(max(dot(n,V),0.),1.,1.333),weight=weights[x]*weights[y];
        distribution+=weight*F*skyRadiance(reflect(-V,n));distributionF+=weight*F;
    }
    if(resolved<=0.){meanF=distributionF;return distribution;}
    bool tent=ambientColor.w>.5;float span=tent?2.:1.,step=span/4.;
    float macro=1.-smoothstep(8.,64.,largeFootprint*step),fine=1.-smoothstep(2.,16.,fineFootprint*step);
    vec3 spatial=vec3(0);float spatialF=0.,total=0.;
    for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        vec2 o=(vec2(x,y)+.5)*step-span*.5;float weight=tent?(1.-abs(o.x))*(1.-abs(o.y)):1.;
        vec3 view=normalize(viewPos-FragPos-px*o.x-py*o.y);
        vec3 n=waterSubpixelNormal(FragTexCoord+du*o.x+dv*o.y,DetailTexCoord+fu*o.x+fv*o.y,macro,fine);
        if(dot(n,view)<0.)n=-n;float F=dielectricFresnel(max(dot(n,view),0.),1.,1.333);
        spatial+=weight*F*skyRadiance(reflect(-view,n));spatialF+=weight*F;total+=weight;
    }
    meanF=mix(distributionF,spatialF/total,resolved);return mix(distribution,spatial/total,resolved);
}
#endif
void main() {
    if(meshBoundary.z>.5&&!waterEyeInside(viewPos,volumeDisplace,fragmentShoreState,waterMask,vec4(seaLevel,surface.z,32,0),meshBoundary.x))discard;
    if(meshBoundary.z>.5){
        // The reduced pass has its own depth target. Reject opaque geometry
        // from the primary capture before integrating otherwise hidden water.
        vec4 occluder=waterQueryPosition(screenUV(FragPos));
        if(occluder.w>.5&&dot(occluder.xyz-FragPos,normalize(FragPos-viewPos))<-.03)discard;
    }
    // Mask follows material UVs, whose V axis is opposite the water grid.
    bool maskWet=texture(waterMask,vec2(WaterMaskCoord.x,1.-WaterMaskCoord.y)).r>=.5;
    vec4 localShore=vec4(0);float shoreWeight=0.;
    if(coastFeatures.z>.5&&abs(coastFeatures.z-2.)>.5){localShore=coastSample(fragmentShoreState,FragPos.xz,coastPatch);shoreWeight=coastBlend(FragPos.xz,localShore,coastPatch,seaLevel);}
    if(!maskWet&&shoreWeight<.01)discard;
    float bedHeight;vec3 bedColor;
    if(coastBedInfo.x>.5&&coastFeatures.z>.5&&waterBed(FragPos.xz,bedHeight,bedColor)&&FragPos.y-bedHeight<.006)discard;
    // Match full-resolution optical normal filtering in the reduced pass.
    float opticalFootprintScale=meshBoundary.z>.5?.5:1.;
    vec2 largeTexels=FragTexCoord*vec2(textureSize(NormalRT,0).xy);
    float largeFootprint=max(length(dFdx(largeTexels)),length(dFdy(largeTexels)))*opticalFootprintScale;
    // The live FFT textures have one mip. Fade unresolved slope toward its
    // up-vector mean to avoid a sparkling/broken band at the far mesh edge.
    float largeResolved=1.-smoothstep(8.,64.,largeFootprint);
    vec3 N=normalize(mix(vec3(0,1,0),waterLargeNormal(FragTexCoord).xyz,largeResolved)),V=normalize(viewPos-FragPos);vec2 fineSlope=vec2(0);
    if(enableDetail!=0) {
        vec3 detail=normalize(waterDetailNormal(DetailTexCoord).xyz);
        vec2 detailTexels=DetailTexCoord*vec2(textureSize(detailNormal,0).xy);
        float detailResolved=1.-smoothstep(2.,16.,max(length(dFdx(detailTexels)),length(dFdy(detailTexels)))*opticalFootprintScale);
        fineSlope=detail.xz/max(detail.y,.1)*detailResolved;
        vec2 slope=N.xz/max(N.y,.1)+fineSlope;
        N=normalize(vec3(slope.x,1,slope.y));
    }
    mat2 unresolvedSlope=mat2(0);float momentFoam=0.,detailMomentFoam=0.;
    vec2 opticalSlope=N.xz/max(N.y,.1);
    #if WATER_FILTERED_LAYOUT
    if(meshBoundary.w>1.5){
        opticalSlope=waterSlopeLookup(NormalRT,FragTexCoord,opticalFootprintScale,unresolvedSlope,momentFoam);
        if(enableDetail!=0){mat2 fineCov;
            fineSlope=waterSlopeLookup(detailNormal,DetailTexCoord,opticalFootprintScale,fineCov,detailMomentFoam);
            opticalSlope+=fineSlope;unresolvedSlope+=fineCov;
        }
        N=normalize(vec3(opticalSlope.x,1,opticalSlope.y));
    }
    #endif
    if(shoreWeight>0.) {
        vec2 dx=vec2(coastPatch.w,0);
        float a=coastElevation(fragmentShoreState,FragPos.xz-dx,coastPatch,seaLevel),b=coastElevation(fragmentShoreState,FragPos.xz+dx,coastPatch,seaLevel);
        float c=coastElevation(fragmentShoreState,FragPos.xz-dx.yx,coastPatch,seaLevel),d=coastElevation(fragmentShoreState,FragPos.xz+dx.yx,coastPatch,seaLevel);
        vec3 shallowN=normalize(vec3(-(b-a)/(2.*coastPatch.w),1,-(d-c)/(2.*coastPatch.w)));
        shallowN=normalize(vec3(shallowN.xz/max(shallowN.y,.1)+fineSlope*smoothstep(.01,.4,localShore.x),1).xzy);
        N=normalize(mix(N,shallowN,shoreWeight));
    }
    N=normalize(vec3(N.xz/max(N.y,.1)+coastParticleSlope(fragmentShoreState,FragPos.xz,coastPatch,coastFeatures.z),1).xzy);
    unresolvedSlope*=pow(1.-shoreWeight,2.);
    if(dot(N,V)<0.0)N=-N;
    vec3 L=normalize(-dirLight.direction);
    float NoV=max(dot(N,V),0.0),NoL=max(dot(N,L),0.0);
    float F0=clamp(outer_FresnelScale,0.0,1.0);
    float fresnel=meshBoundary.w>.5?dielectricFresnel(NoV,1.,1.333):F0+(1.0-F0)*pow(1.0-NoV,5.0);
    float foam=clamp(waterLargeNormal(FragTexCoord).a,0.0,1.0)*largeResolved;
    if(enableDetail!=0&&meshBoundary.w<1.5){vec2 at=DetailTexCoord*vec2(textureSize(detailNormal,0).xy);float resolved=1.-smoothstep(2.,16.,max(length(dFdx(at)),length(dFdy(at)))*opticalFootprintScale);
        foam=1.0-(1.0-foam)*(1.0-clamp(waterDetailNormal(DetailTexCoord).w,0.0,1.0)*resolved);}
    if(meshBoundary.w>1.5)foam=1.-(1.-clamp(momentFoam,0.,1.))*(1.-clamp(detailMomentFoam,0.,1.));
    if(shoreWeight>0.&&coastFeatures.w>.5){float coastalFoam=texture(shoreFoam,(FragPos.xz-coastPatch.xy)/coastPatch.z).r;
        vec2 velocity=coastFeatures.z>1.5?vec2(0):localShore.yz/max(localShore.x,.02),foamUV=FragPos.xz-velocity*.15;
        float pattern=.65*foamNoise(foamUV*2.)+.35*foamNoise(foamUV*9.);
        float footprint=max(length(dFdx(foamUV)),length(dFdy(foamUV)));
        float broken=coastalFoam*smoothstep(.15,.8,coastalFoam-pattern*.55+.25);
        coastalFoam=mix(broken,coastalFoam*.8,smoothstep(.1,.6,footprint));
        foam=1.-(1.-foam)*(1.-coastalFoam*shoreWeight);}
    vec3 refractionN=N;
    if(coastFeatures.x>.5){float variation=(dot(dFdx(N),dFdx(N))+dot(dFdy(N),dFdy(N)))*opticalFootprintScale*opticalFootprintScale;
        vec3 macro=normalize(waterLargeNormal(FragTexCoord).xyz);if(dot(macro,V)<0.)macro=-macro;
        refractionN=normalize(mix(N,macro,clamp(variation*16.,0.,.75)*(1.-shoreWeight)));}
    if(underwaterControls.x>.5&&waterEyeInside(viewPos,volumeDisplace,fragmentShoreState,waterMask,vec4(seaLevel,surface.z,32,0),meshBoundary.x)){
        // Normal points into the incident medium. A local wave slope may face
        // away even though the eye is below the macro surface.
        if(dot(refractionN,V)<0.)refractionN=-refractionN;
        float interfaceF;vec3 result=underwaterInterface(refractionN,V,L,interfaceF);
        vec3 foamLight=meshBoundary.z>.5?vec3(0):outer_BubblesColor*(dirLight.diffuse*max(L.y,0.)/PI+skyRadiance(vec3(0,1,0))*.25);
        result=mix(result,meshBoundary.z>.5?vec3(0):foamLight,foam);
        result=underwaterSegment(result,viewPos,normalize(FragPos-viewPos),lastWaterDistance,L,false);
        if(volumeCacheAccepted)result+=volumeCachedScatter;
        if(meshBoundary.z>.5){
            FragColor=vec4(max(result,vec3(0)),lastWaterDistance);
            TemporalMotion=vec4(volumeEncodeNormal(refractionN),volumeReflectedDistance,float(volumeReflectedKind)*4.+interfaceF);return;
        }
        if(absorb.w>0.)result=absorb.w<1.5?lastWaterT:absorb.w<2.5?vec3(lastWaterDistance/deepWaterDistance):absorb.w<3.5?vec3(lastWaterHit?1.:0.):absorb.w<4.5?vec3(lastWaterSource==1.?1.:0.,lastWaterSource==3.?1.:0.,lastWaterSource==0.?1.:0.):vec3(lastWaterConfidence);
        // Screen-space hits and rapidly changing critical-angle boundaries are
        // unreliable optical histories; explicitly reject the previous optical image.
        TemporalMotion=vec4(0,0,0,-1);FragColor=vec4(max(result,vec3(0)),1);return;
    }
    vec3 body=transmittedWater(refractionN,V,L);
    // Explicit diagnostics use linear HDR; they do not alter optical parameters.
    if(absorb.w>0.&&absorb.w<5.5){vec3 diagnostic=absorb.w<1.5?lastWaterT:absorb.w<2.5?vec3(lastWaterDistance/deepWaterDistance):absorb.w<3.5?vec3(lastWaterHit?1.:0.):absorb.w<4.5?vec3(lastWaterSource==1.?1.:0.,lastWaterSource==2.?1.:0.,lastWaterSource==0.?1.:0.):vec3(lastWaterConfidence);
        FragColor=vec4(diagnostic,1);TemporalMotion=vec4(0);return;}

    vec3 reflectedRay=normalize(reflect(-V,N));
    vec3 reflection=skyRadiance(reflectedRay);
    float reflectionConfidence=1.;bool reflectedObject=false;
    if(volume.w>.5&&airCapture.z>1.5&&reflectedRay.y>0.){
        queryAboveWater=true;queryReflection=true;vec2 hitUV;float hitDistance,confidence;
        if(waterDDA(FragPos,V,reflectedRay,underwaterControls.w,hitUV,hitDistance,confidence)){
            float edge=min(min(hitUV.x,1.-hitUV.x),min(hitUV.y,1.-hitUV.y));
            float blend=smoothstep(0.,.015,edge);
            reflection=mix(reflection,waterQueryColor(hitUV),blend);reflectionConfidence=confidence*blend;reflectedObject=true;
        }
        queryAboveWater=false;queryReflection=false;
    }
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
    #if WATER_FILTERED_LAYOUT
    vec2 du=dFdx(FragTexCoord)*opticalFootprintScale,dv=dFdy(FragTexCoord)*opticalFootprintScale,fu=dFdx(DetailTexCoord)*opticalFootprintScale,fv=dFdy(DetailTexCoord)*opticalFootprintScale;
    vec3 px=dFdx(FragPos)*opticalFootprintScale,py=dFdy(FragPos)*opticalFootprintScale;
    vec2 slopeDx=dFdx(opticalSlope)*opticalFootprintScale,slopeDy=dFdy(opticalSlope)*opticalFootprintScale;
    float fineFootprint=max(length(fu*vec2(textureSize(detailNormal,0).xy)),length(fv*vec2(textureSize(detailNormal,0).xy)));
    #endif
    if(meshBoundary.w>.5){
        if(specularColor.w<.02){
            mat2 angularCov=meshBoundary.w>1.5?waterSlopeSolarCovariance(opticalSlope,unresolvedSlope,V,L):mat2(0);
            float solar=waterSolarAngularKernelCov(reflectedRay,L,sunDisplay.w,dFdx(reflectedRay)*opticalFootprintScale,dFdy(reflectedRay)*opticalFootprintScale,angularCov);
            sun=NoL>0.?dirLight.diffuse*fresnel*solar:vec3(0);
            #if WATER_FILTERED_LAYOUT
            if(meshBoundary.w>2.5&&shoreWeight<=0.){
                mat2 covariance=unresolvedSlope+(outerProduct(slopeDx,slopeDx)+outerProduct(slopeDy,slopeDy))/12.;
                if(max(largeFootprint,enableDetail!=0?fineFootprint:0.)>4.&&determinant(covariance)>1e-16)
                    sun=waterSmoothSunSlopes(V,L,dirLight.diffuse,sunDisplay.w,opticalSlope,covariance);
            }
            #endif
        }else if(meshBoundary.w>1.5){
            vec2 sx=dFdx(opticalSlope)*opticalFootprintScale,sy=dFdy(opticalSlope)*opticalFootprintScale;
            sun=waterRoughSunMoments(N,V,L,dirLight.diffuse,sunDisplay.w,specularColor.w,opticalSlope,unresolvedSlope+(outerProduct(sx,sx)+outerProduct(sy,sy))/12.);
        }else sun=waterRoughSun(N,V,L,dirLight.diffuse,sunDisplay.w,specularColor.w);
    }
    #if WATER_FILTERED_LAYOUT
    if(meshBoundary.w>1.5&&shoreWeight<=0.){
        float footprint=max(largeFootprint,enableDetail!=0?fineFootprint:0.);
        float resolved=1.-smoothstep(4.,8.,footprint);
        if(resolved>0.)sun=mix(sun,waterPixelSun(L,du,dv,fu,fv,px,py,largeFootprint,fineFootprint),resolved);
    }
    #endif
    vec3 reflectedContribution=fresnel*reflection;float bodyFresnel=fresnel;
    #if WATER_FILTERED_LAYOUT
    if(meshBoundary.w>3.5&&specularColor.w<.02&&!reflectedObject&&shoreWeight<=0.){
        reflectedContribution=waterPixelSky(V,opticalSlope,unresolvedSlope+(outerProduct(slopeDx,slopeDx)+outerProduct(slopeDy,slopeDy))/12.,
            du,dv,fu,fv,px,py,largeFootprint,fineFootprint,bodyFresnel);
    }
    #endif
    vec3 foamDiffuse=outer_BubblesColor*(dirLight.diffuse*NoL/PI+skyRadiance(N)*0.25);
    vec3 result=mix((1.0-bodyFresnel)*body+reflectedContribution+sun,foamDiffuse,foam);
    // Blend the finite kilometre-scale mesh border into the sky on the same
    // viewing ray. Near-shore transport is unaffected; this is a display-domain
    // seam treatment, not an atmospheric scattering integral.
    float boundaryFade=meshBoundary.x>=512. && hasSky!=0?smoothstep(.8,.995,max(abs(FragPos.x),abs(FragPos.z))/meshBoundary.x):0.;
    result=mix(result,skyRadiance(-V),boundaryFade);
    // Additive radiance diagnostics include the actual foam / border weights.
    // Their sum equals the beauty on covered water before temporal filtering.
    if(absorb.w>5.5){
        float weight=(1.-foam)*(1.-boundaryFade);
        result=absorb.w<6.5?weight*(1.-bodyFresnel)*body:
               absorb.w<7.5?weight*reflectedContribution:
               absorb.w<8.5?weight*sun:
               absorb.w<9.5?(1.-boundaryFade)*foam*foamDiffuse:
               absorb.w<10.5?boundaryFade*skyRadiance(-V):
               vec3(reflectedRay.y<0.?1.:0.);
    }
    TemporalMotion=vec4(0);
    if(temporalActive!=0 && PreviousClip.w>0.0)TemporalMotion=vec4(PreviousClip.xy/PreviousClip.w*vec2(.5,-.5)+.5-screenUV(FragPos),PreviousDepth,1);
    if(coastFeatures.x>.5)TemporalMotion.w*=lastWaterConfidence;
    TemporalMotion.w*=min(1.-boundaryFade,reflectionConfidence);
    if(TemporalMotion.w<=0.)TemporalMotion.w=-1.;
    // Values (3,4] identify filtered physical water while retaining its optical
    // confidence. Ordinary opaque motion uses 2, legacy water uses [0,1].
    else if(meshBoundary.w>1.5&&!lastWaterHit&&!reflectedObject&&shoreWeight<=0.)TemporalMotion.w+=3.;
    FragColor=vec4(max(result,vec3(0)),1); // Tone mapping belongs to the HDR post pass.
}
