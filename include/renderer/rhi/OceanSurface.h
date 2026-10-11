#pragma once
#include "component/WaterSurfaceModel.h"
#include "renderer/rhi/GpuOcean.h"
#include "component/ShoreWaterSettings.h"
#include "component/WaveParticleSettings.h"
namespace render {
struct FrameData;
struct AerialParameters;
struct ImageRGBA8;
struct DrawPacket;
class GpuImage;
class GpuImageCache;
struct WaterBathymetry {
    uint32_t size=0;
    // Endpoint-preserving local terrain height and encoded base colour. Fixed
    // CPU source mip, independent of camera LOD, resident VT pages and GPU mesh.
    std::vector<glm::vec4> heightColor;
};
struct VirtualTextureSource;
std::shared_ptr<const WaterBathymetry> prepareWaterBathymetry(const VirtualTextureSource&,const VirtualTextureSource&,glm::vec3 albedoFactor=glm::vec3(1));
struct OceanSurfaceSettings {
    WaterSurfaceModel surfaceModel;
    uint64_t id=1;OceanSettings spectrum;uint32_t meshSize=129;float surfaceLength=0;
    float seaLevel=-5,timeScale=1,detailStrength=1,refractionStrength=1,deepWaterDistance=40,subsurfaceStrength=1,anisotropy=.65f,fresnel=.02f,gloss=256;
    bool detailWaves=true,refraction=true,animate=true;
    bool cameraGrid=true,underwaterCapture=true,volumeIntegration=true;
    bool robustRefraction=false,multipleScattering=false;
    bool underwaterView=true,underwaterFog=true;
    bool sceneReflection=true; // Mirrored scene capture for above-water reflections.
    bool underwaterWideRefraction=true; // Recover refracted air geometry outside the camera frustum.
    bool underwaterParticles=false;
    bool underwaterSunShafts=false;
    float sunShaftStrength=1; // Contrast of refracted solar flux in the volume, 0..3.
    int underwaterVolumeDownsample=1; // Optional guided interface scattering; background remains full resolution.
    int underwaterVolumeSteps=4; // Eye and submerged surface paths, 4..32.
    float particleDensity=.35f; // Occupancy of world-space suspended sediment cells.
    bool shortWaveRipples=false;
    bool bedCaustics=false;
    bool causticCascades=true,causticMeshReceivers=true;
    float causticStrength=1;
    float rippleRmsHeight=.025f; // Metres, before Small wave detail multiplier.
    ShoreWaterSettings shore;
    WaveParticleSettings waveParticles;
    std::shared_ptr<const WaterBathymetry> bathymetry;
    glm::mat4 bathymetryModel{1};
    float gridFocus=8;
    uint32_t opticalDebug=0; // 1-5 optical guides; 6-10 body/reflection/sun/foam/border radiance; 11 downward reflection.
    std::shared_ptr<const ImageRGBA8> waterMask;
    glm::vec3 absorption{.12f,.04f,.02f},scattering{.025f,.05f,.07f};
    glm::vec3 shallow=glm::pow(glm::vec3(.30713776f,.4703595f,.5471698f),glm::vec3(2.2f));
    glm::vec3 deep=glm::pow(glm::vec3(.0499288f,.1436479f,.20754719f),glm::vec3(2.2f));
    glm::vec3 foamColor{1},specular=glm::pow(glm::vec3(.3962264f,.3943574f,.3943574f),glm::vec3(2.2f)),ambient{0};
};
inline OceanSettings oceanDetailSettings(const OceanSurfaceSettings& s){
    auto d=s.spectrum;d.size=256;d.length=32;d.amplitude*=.06f*s.detailStrength;
    d.seed+=71;d.windSpeed*=.6f;d.previousWind*=.6f;
    if(s.shortWaveRipples||s.spectrum.seaState.enabled){
        d.seaState.enabled=false;d.minWavelength=.5f;d.maxWavelength=2;
        d.targetRmsHeight=s.rippleRmsHeight*s.detailStrength;d.foamScale=0;
    }
    return d;
}
class OceanSurface {
public:
    OceanSurface(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,const OceanSurfaceSettings&);
    ~OceanSurface();
    bool compatible(const OceanSurfaceSettings&)const;
    void simulate(float seconds,const OceanSurfaceSettings&,glm::vec3 cameraPosition=glm::vec3(0));
    rhi::TextureViewHandle wetnessView() const;
    glm::vec4 shorePatch() const;
    std::vector<float> readShore(bool foam=false) const;
    uint32_t shoreSubsteps() const;
    uint32_t waveParticleCount() const;
    void captureUnderwater(Resources&,rhi::CommandList&,const FrameData&,const OceanSurfaceSettings&,
                          const std::vector<DrawPacket>&,rhi::BufferHandle camera,rhi::BufferHandle lighting,
                          const std::vector<rhi::BindingEntry>& environment,uint32_t width,uint32_t height,rhi::TextureViewHandle scenePosition,const AerialParameters&,rhi::TextureViewHandle transmittance,rhi::TextureViewHandle multiple,rhi::BufferHandle shadows,rhi::TextureViewHandle shadowAtlas);
    void record(Resources& frame,rhi::CommandList&,const FrameData&,const OceanSurfaceSettings&,rhi::TextureViewHandle sky,rhi::TextureViewHandle opaque,rhi::TextureViewHandle position,rhi::TextureViewHandle normal,rhi::BufferHandle shadowParameters,rhi::TextureViewHandle shadowAtlas);
    void recordVolume(Resources&,rhi::CommandList&,const FrameData&,const OceanSurfaceSettings&,rhi::TextureViewHandle sky,rhi::TextureViewHandle opaque,rhi::TextureViewHandle position,rhi::TextureViewHandle normal,rhi::BufferHandle shadowParameters,rhi::TextureViewHandle shadowAtlas);
    void recordUnderwaterFog(Resources&,rhi::CommandList&,const FrameData&,const OceanSurfaceSettings&,
                            rhi::TextureViewHandle target,rhi::TextureViewHandle opaque,rhi::TextureViewHandle position,rhi::TextureViewHandle normal,rhi::TextureViewHandle sky,
                            rhi::BufferHandle shadowParameters,rhi::TextureViewHandle shadowAtlas,rhi::TextureViewHandle surfaceDepth);
    std::vector<float> readCaustics(uint32_t cascade=0) const;
    std::vector<float> readCapture(bool positions=false,bool aboveWater=false) const;
private:
    rhi::TextureViewHandle coastalState(bool previous=false) const;
    glm::vec4 previousShorePatch() const;
    float coastalMode() const;
    struct WaterVolume;std::unique_ptr<WaterVolume> volume_;rhi::PipelineHandle volumePipeline_;rhi::GraphicsPipelineDesc volumeDesc_;bool volumeReady_=false,volumePass_=false;
    struct WaterDepth;std::unique_ptr<WaterDepth> fallbackDepth_;bool fallbackDepthReady_=false;
    struct WaterTargets;std::unique_ptr<WaterTargets> underwater_;
    std::unique_ptr<class GpuWaterSunShafts> sunShafts_;bool sunShaftsReady_=false;
    std::unique_ptr<class GpuWaterCaustics> caustics_;bool causticsReady_=false;
    rhi::PipelineHandle capturePipeline_,captureInstanced_;
    rhi::ComputePipelineHandle airAtmospherePipeline_;std::vector<rhi::BindingLayout> airAtmosphereLayouts_;
    rhi::BindingLayout captureLayout_;
    rhi::PipelineHandle fogPipeline_;rhi::BufferHandle fogQuad_;
    struct Simulation;std::unique_ptr<Simulation> simulation_;
    struct Coastal;std::unique_ptr<Coastal> coastal_;
    struct Transport;std::unique_ptr<Transport> transport_;
    std::string directory_;
    Resources resources_;OceanSurfaceSettings initial_;rhi::PipelineHandle pipeline_;rhi::BufferHandle vertices_,indices_;uint32_t indexCount_;
    rhi::SamplerHandle repeat_,clamp_,positionSampler_,slopeSampler_;std::array<rhi::TextureHandle,2> previous_;std::array<rhi::TextureViewHandle,2> previousViews_;
    glm::mat4 previousVP_{1},previousView_{1},previousModel_{1};bool history_=false;
    glm::vec4 previousGrid_{0};
    bool captured_=false,opaqueGeometry_=true;
    glm::mat4 airVP_{1},airView_{1},reflectionVP_{1},reflectionView_{1};
    int reflectionColumn_=0;float airFocal_=1;
    std::shared_ptr<GpuImageCache> imageCache_;
    std::shared_ptr<GpuImage> mask_;
};
void validateWaterSurface(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
