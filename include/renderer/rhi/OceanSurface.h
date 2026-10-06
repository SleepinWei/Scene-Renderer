#pragma once
#include "renderer/rhi/GpuOcean.h"
#include "component/ShoreWaterSettings.h"
namespace render {
struct FrameData;
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
std::shared_ptr<const WaterBathymetry> prepareWaterBathymetry(const VirtualTextureSource&,const VirtualTextureSource&);
struct OceanSurfaceSettings {
    uint64_t id=1;OceanSettings spectrum;uint32_t meshSize=129;float surfaceLength=0;
    float seaLevel=-5,timeScale=1,detailStrength=1,refractionStrength=1,deepWaterDistance=40,subsurfaceStrength=1,anisotropy=.65f,fresnel=.02f,gloss=256;
    bool detailWaves=true,refraction=true,animate=true;
    bool cameraGrid=true,underwaterCapture=true,volumeIntegration=true;
    bool robustRefraction=false,multipleScattering=false;
    bool underwaterView=true,underwaterFog=true;
    bool shortWaveRipples=false;
    float rippleRmsHeight=.025f; // Metres, before Small wave detail multiplier.
    ShoreWaterSettings shore;
    std::shared_ptr<const WaterBathymetry> bathymetry;
    glm::mat4 bathymetryModel{1};
    float gridFocus=8;
    uint32_t opticalDebug=0; // 1 transmittance, 2 path length, 3 refraction hit.
    std::shared_ptr<const ImageRGBA8> waterMask;
    glm::vec3 absorption{.12f,.04f,.02f},scattering{.025f,.05f,.07f};
    glm::vec3 shallow=glm::pow(glm::vec3(.30713776f,.4703595f,.5471698f),glm::vec3(2.2f));
    glm::vec3 deep=glm::pow(glm::vec3(.0499288f,.1436479f,.20754719f),glm::vec3(2.2f));
    glm::vec3 foamColor{1},specular=glm::pow(glm::vec3(.3962264f,.3943574f,.3943574f),glm::vec3(2.2f)),ambient{0};
};
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
    void captureUnderwater(Resources&,rhi::CommandList&,const FrameData&,const OceanSurfaceSettings&,
                          const std::vector<DrawPacket>&,rhi::BufferHandle camera,rhi::BufferHandle lighting,
                          const std::vector<rhi::BindingEntry>& environment,uint32_t width,uint32_t height);
    void record(Resources& frame,rhi::CommandList&,const FrameData&,const OceanSurfaceSettings&,rhi::TextureViewHandle sky,rhi::TextureViewHandle opaque,rhi::TextureViewHandle position,rhi::TextureViewHandle normal,rhi::BufferHandle shadowParameters,rhi::TextureViewHandle shadowAtlas);
    void recordUnderwaterFog(Resources&,rhi::CommandList&,const FrameData&,const OceanSurfaceSettings&,
                            rhi::TextureViewHandle target,rhi::TextureViewHandle opaque,rhi::TextureViewHandle position,rhi::TextureViewHandle normal,rhi::TextureViewHandle sky,
                            rhi::BufferHandle shadowParameters,rhi::TextureViewHandle shadowAtlas);
    std::vector<float> readCapture(bool positions=false,bool aboveWater=false) const;
private:
    struct WaterTargets;std::unique_ptr<WaterTargets> underwater_;
    rhi::PipelineHandle capturePipeline_,captureInstanced_;
    rhi::BindingLayout captureLayout_;
    rhi::PipelineHandle fogPipeline_;rhi::BufferHandle fogQuad_;
    struct Simulation;std::unique_ptr<Simulation> simulation_;
    struct Coastal;std::unique_ptr<Coastal> coastal_;
    struct Transport;std::unique_ptr<Transport> transport_;
    std::string directory_;
    Resources resources_;OceanSurfaceSettings initial_;rhi::PipelineHandle pipeline_;rhi::BufferHandle vertices_,indices_;uint32_t indexCount_;
    rhi::SamplerHandle repeat_,clamp_,positionSampler_;std::array<rhi::TextureHandle,2> previous_;std::array<rhi::TextureViewHandle,2> previousViews_;
    glm::mat4 previousVP_{1},previousView_{1},previousModel_{1};bool history_=false;
    glm::vec4 previousGrid_{0};
    bool captured_=false,opaqueGeometry_=true;
    std::shared_ptr<GpuImageCache> imageCache_;
    std::shared_ptr<GpuImage> mask_;
};
void validateWaterSurface(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
