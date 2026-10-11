#pragma once
#include "PT/CpuPathTracer.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <type_traits>
namespace pt {
// Exact scalar tokens, excluding padding, camera projection and display settings.
// Payload identity/revision relies on the immutable snapshot contract.
struct SceneIdentity {
    std::vector<uint64_t> transport,procedural;
    bool operator==(const SceneIdentity &other)const{return transport==other.transport&&procedural==other.procedural;}
};
namespace identity_detail {
struct Tokens {
    std::vector<uint64_t> values;
    template<class T,std::enable_if_t<std::is_arithmetic_v<T>,int> =0> void add(T v){uint64_t bits=0;std::memcpy(&bits,&v,sizeof(v));values.push_back(bits);}
    template<class T,glm::precision Q> void add(const glm::tvec2<T,Q> &v){for(int i=0;i<2;++i)add(v[i]);}
    template<class T,glm::precision Q> void add(const glm::tvec3<T,Q> &v){for(int i=0;i<3;++i)add(v[i]);}
    template<class T,glm::precision Q> void add(const glm::tvec4<T,Q> &v){for(int i=0;i<4;++i)add(v[i]);}
    void add(const glm::mat4 &v){for(int i=0;i<4;++i)add(v[i]);}
    template<class T> void add(const std::shared_ptr<const T> &p){add(uint64_t(reinterpret_cast<uintptr_t>(p.get())));}
    void add(const render::MaterialParameters &v){add(v.albedoAlpha);add(v.factors);add(v.emissiveNormal);}
    void add(const render::MaterialExtension &v){add(v.lobes);add(v.settings);add(v.features);add(v.shoreHeight);add(v.shoreSurface);add(v.specularShape);for(const auto& cell:v.rainWetCells)add(cell);}
    void add(const VegetationSettings &v){
#define PT_TOKEN(member) add(v.member)
        PT_TOKEN(capacity);PT_TOKEN(maxLod);PT_TOKEN(samplesPerCell);PT_TOKEN(density);PT_TOKEN(distance);PT_TOKEN(fadeStart);PT_TOKEN(minimumNormalY);PT_TOKEN(waterLevel);PT_TOKEN(shoreMargin);PT_TOKEN(heightScale);PT_TOKEN(widthScale);PT_TOKEN(maximumAltitude);PT_TOKEN(nearSpacing);PT_TOKEN(farSpacing);PT_TOKEN(denseRadius);PT_TOKEN(sparseRadius);PT_TOKEN(exclusionSeaLevel);PT_TOKEN(exclusionHeightRange);PT_TOKEN(exclusionSlopeMin);PT_TOKEN(exclusionSlopeMax);PT_TOKEN(frustumCull);PT_TOKEN(environmentWind);if(v.environmentWind){PT_TOKEN(windX);PT_TOKEN(windZ);PT_TOKEN(windPhase);}
#undef PT_TOKEN
        add(uint64_t(v.waterMaskPath.size()));for(unsigned char c:v.waterMaskPath)add(c);
    }
};
}
inline SceneIdentity sceneIdentity(const render::RenderWorldSnapshot &world){
    identity_detail::Tokens t,p;const auto &f=world.frame;
    t.add(uint64_t(world.draws.size()));
    for(const auto &d:world.draws){
        t.add(d.objectId);t.add(d.mesh);if(d.mesh){t.add(d.mesh->id);t.add(d.mesh->revision);}
        t.add(d.material);if(d.material){t.add(d.material->id);t.add(d.material->revision);for(const auto &image:d.material->images)t.add(image);t.add(d.material->special);t.add(d.material->height);}
        t.add(d.model);t.add(d.parameters);t.add(d.extension);
        t.add(d.pathTracingFiber);t.add(d.pathTracingSheen);t.add(d.pathTracingSheenColor);t.add(d.pathTracingKnit);t.add(d.pathTracingIor);t.add(d.pathTracingRoughness);t.add(d.pathTracingBsdfModel);t.add(d.pathTracingDiffuseTransmission);t.add(d.pathTracingTransmissionTexture);t.add(d.pathTracingRoughnessTexture);t.add(d.pathTracingAbsorption);t.add(d.pathTracingScattering);t.add(d.pathTracingAnisotropy);t.add(d.pathTracingSpecularAnisotropy);t.add(d.pathTracingSpecularRotation);t.add(d.pathTracingNormalScale);t.add(d.pathTracingKind);for(const auto &image:d.pathTracingShoreline)t.add(image);
    }
    t.add(f.directionalEnabled);t.add(f.inverseSquareLocalLights);t.add(uint64_t(f.lights.size()));
    for(const auto &light:f.lights){t.add(light.positionType);t.add(light.colorInner);t.add(light.directionOuter);}
    t.add(f.sky);if(f.sky){const auto &a=f.atmosphere;t.add(a.radii);t.add(a.densities);t.add(a.rayleigh);t.add(a.mie);t.add(a.extinction);t.add(a.absorption);t.add(f.sunAngle);t.add(f.sunAzimuth);t.add(f.multipleScattering);t.add(f.groundAlbedo);const float height=(f.cameraPosition.y-f.seaLevelMeters)*.001f,top=a.radii.z-a.radii.w-.001f;t.add(std::isfinite(top)&&top>=.001f?std::clamp(height,.001f,top):height);}
    p.add(bool(world.terrain));if(world.terrain){const auto &v=*world.terrain;p.add(v.source);if(v.source){p.add(v.source->id);p.add(v.source->revision);}p.add(v.model);p.add(v.parameters);p.add(v.extension);p.add(bool(v.vegetation));if(v.vegetation)p.add(*v.vegetation);}
    p.add(uint64_t(f.oceans.size()));for(const auto &o:f.oceans){const auto &s=o.spectrum;
        p.add(o.id);p.add(s.size);p.add(s.length);p.add(s.amplitude);p.add(s.windSpeed);p.add(s.choppiness);p.add(s.heightScale);p.add(s.foamScale);p.add(s.foamThreshold);p.add(s.windDirection);p.add(s.seed);p.add(s.minWavelength);p.add(s.maxWavelength);p.add(s.targetRmsHeight);p.add(s.windTransition);if(s.windTransition){p.add(s.previousWind);p.add(s.windBlend);}
        const auto& a=s.seaState;p.add(a.enabled);if(a.enabled){p.add(a.windHeight);p.add(a.windPeriod);p.add(a.windGamma);p.add(a.windSpread);p.add(a.swellHeight);p.add(a.swellPeriod);p.add(a.swellSpread);p.add(a.swellDirection);p.add(a.depth);}
        if(a.enabled){p.add(o.cameraGrid);if(o.cameraGrid){p.add(o.gridFocus);p.add(glm::vec2(f.cameraPosition.x,f.cameraPosition.z));}}
        p.add(o.surfaceModel.physical);if(o.surfaceModel.physical)p.add(o.surfaceModel.effectiveRoughness());
        p.add(o.shortWaveRipples);p.add(o.rippleRmsHeight);
        p.add(o.meshSize);p.add(o.surfaceLength);p.add(o.seaLevel);p.add(o.timeScale);p.add(o.animate);p.add(o.detailWaves);p.add(o.detailStrength);p.add(o.waterMask);p.add(o.absorption);p.add(o.scattering);p.add(o.anisotropy);p.add(o.refraction);if(!o.refraction)p.add(o.deep);
    }
    t.add(f.clouds.enabled);if(f.clouds.enabled){const auto& c=f.clouds;
        t.add(c.voxel);t.add(c.baseHeight);t.add(c.thickness);t.add(c.coverage);t.add(c.density);t.add(c.shapeScale);t.add(c.weatherScale);t.add(c.erosion);t.add(c.maxDistance);t.add(c.volumeCenter);t.add(c.volumeSize);t.add(c.voxelResolution);t.add(c.seed);t.add(c.storm);t.add(c.lightning);if(c.lightning>0)t.add(f.timeSeconds);t.add(c.dynamicCoverage);t.add(c.steps);t.add(c.lightSteps);t.add(c.advection.has_value());if(c.advection)t.add(*c.advection);else {t.add(c.wind);t.add(f.timeSeconds);}t.add(f.cameraPosition);
    }
    if(world.terrain||!f.oceans.empty())p.add(f.timeSeconds);
    return {std::move(t.values),std::move(p.values)};
}
inline bool sameRenderOptions(const Options &a,const Options &b){
    if(a.sppm!=b.sppm||a.sppmAlpha!=b.sppmAlpha)return false;
    // Exposure/CPU thread count do not affect the GPU raw film.
#define PT_OPTION(member) if(a.member!=b.member)return false
    PT_OPTION(width);PT_OPTION(height);PT_OPTION(samples);PT_OPTION(maxDepth);PT_OPTION(seed);PT_OPTION(sobol);PT_OPTION(adaptive);PT_OPTION(waterSunProposal);PT_OPTION(thinSunProposal);PT_OPTION(minimumSamples);PT_OPTION(relativeError);PT_OPTION(absoluteError);PT_OPTION(guiding);PT_OPTION(radianceCache);PT_OPTION(bdpt);PT_OPTION(photonMapping);PT_OPTION(photonPaths);PT_OPTION(gpuBatchSamples);PT_OPTION(shadowAnyHit);PT_OPTION(gpuTraversal);PT_OPTION(photonRadius);PT_OPTION(trainingSamples);PT_OPTION(cacheMinimum);PT_OPTION(cacheDepth);PT_OPTION(guideCellSize);PT_OPTION(checkpointSamples);
    PT_OPTION(worldLod.enabled);PT_OPTION(worldLod.maxError);PT_OPTION(worldLod.targetRatio);PT_OPTION(worldLod.maxUvError);PT_OPTION(worldLod.levels);
#undef PT_OPTION
    return true;
}
}
