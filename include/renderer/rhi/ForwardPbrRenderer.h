#pragma once
#include "renderer/rhi/GpuMesh.h"
#include "renderer/rhi/GpuMaterial.h"
#include "renderer/rhi/GpuAtmosphere.h"
#include "renderer/rhi/OceanSurface.h"
#include "renderer/rhi/GpuTemporal.h"
namespace render {
struct alignas(16) LightData {
    glm::vec4 positionType{0}; // 0 directional, 1 point, 2 spot; legacy constant point attenuation.
    glm::vec4 colorInner{0};
    glm::vec4 directionOuter{0};
};
struct RsmSettings {
    bool useSunSky=true,sunBounce=true,skyBounce=true,indirectOnly=false;
    float worldRadius=20,intensity=1,sampleRadius=.3f,minDistance=.1f;
    int sampleCount=128;
};
struct ShadowSettings {
    bool pcss=true;
    float distance=300, cascadeBlend=.1f, depthBias=.002f;
    float sunAngularRadius=.00465f, localLightRadius=.05f, maxFilterTexels=24;
};
struct FrameData {
    glm::mat4 viewProjection{1}; // NDC depth 0..1.
    glm::mat4 view{1};
    float nearPlane=.1f,farPlane=100;
    glm::vec3 cameraPosition{0,0,3};
    float ambient = .03f;
    bool shadows=false,ssao=false,rsm=false,sky=false,taa=false;
    uint64_t historyKey=0;
    uint32_t viewportWidth=1280,viewportHeight=720;
    float aoRadius=1,aoBias=.025f,aoPower=1.5f;
    bool toneMapping=true,forwardShading=false;
    RsmSettings rsmSettings;
    ShadowSettings shadowSettings;
    bool directionalEnabled=true;
    bool inverseSquareLocalLights=false; // Legacy forward validation can retain constant point attenuation.
    AtmosphereSettings atmosphere;float sunAngle=10,sunAzimuth=0,seaLevelMeters=0,multipleScattering=1,groundAlbedo=.2f,timeSeconds=0;
    std::vector<OceanSurfaceSettings> oceans;
    std::vector<LightData> lights;
};
struct DrawPacket {
    std::shared_ptr<GpuMesh> mesh;
    std::shared_ptr<GpuMaterial> material;
    glm::mat4 model{1};
    uint64_t id=0;
    bool wireframe=false;
    bool reliableGeneratedHistory=false;
};
enum class PbrPath { Forward, Deferred, Scene };
class ShadowRenderer;
class ForwardPbrRenderer {
public:
    ForwardPbrRenderer(std::shared_ptr<rhi::GraphicsDevice>, const std::string& shaderDirectory, uint32_t width, uint32_t height, PbrPath path = PbrPath::Forward);
    ~ForwardPbrRenderer();
    void resize(uint32_t width, uint32_t height);
    void resetTemporal(); // Start an independent capture or comparison.
    void render(const FrameData&, const std::vector<DrawPacket>&, float exposure = 1, float gamma = 2.2f);
    rhi::TextureHandle output() const;
    rhi::TextureViewHandle depthView() const;
    std::vector<glm::mat4> shadowVisibilityViews() const;
    glm::mat4 renderedViewProjection() const {return renderedVP_;}
    std::vector<float> readHDR();
    std::vector<uint8_t> readOutput();
    std::vector<float> readBackDepth();
    std::vector<float> readSSAO();
    std::vector<float> readShadowDepth();
    std::vector<float> readGBuffer(uint32_t attachment);
    const rhi::GraphicsDevice* owner() const { return resources_.device.get(); }
    static rhi::BindingLayout frameLayout();
private:
    struct Targets;
    struct ObjectSlot { rhi::BufferHandle data;rhi::BindingSetHandle bindings; };
    Resources resources_;
    std::unique_ptr<ShadowRenderer> shadows_;
    std::unique_ptr<GpuAtmosphere> atmosphere_;
    std::map<uint64_t,std::unique_ptr<OceanSurface>> oceans_;
    std::string directory_;
    std::unique_ptr<GpuTemporal> temporal_;
    std::unique_ptr<Targets> targets_;
    rhi::PipelineHandle motionPipeline_,motionInstanced_,backDepthPipeline_,transparentPipeline_,transparentInstanced_;
    rhi::BindingLayout motionLayout_,transparentLayout_;
    std::map<uint64_t,glm::mat4> previousModels_;
    glm::mat4 renderedVP_{1};
    glm::vec3 previousCamera_{0};glm::mat4 previousProjection_{1};uint64_t historyKey_=0;bool previousTaa_=false,temporalOutput_=false;
    rhi::BufferHandle skyParameters_;rhi::TextureViewHandle skyView_,irradianceView_;
    rhi::BufferHandle effects_;
    rhi::BindingSetHandle effectsBindings_;
    rhi::PipelineHandle ssaoPipeline_,sceneForward_,sceneForwardInstanced_,sceneForwardWire_,sceneForwardWireInstanced_;
    rhi::BindingLayout sceneForwardLayout_;
    rhi::BufferHandle camera_, lighting_, tone_, quad_;
    rhi::PipelineHandle forward_, instanced_,wireframe_,wireframeInstanced_,tonePipeline_, deferredPipeline_;
    PbrPath path_;
    rhi::BindingLayout geometryLayout_;
    rhi::SamplerHandle hdrSampler_,skySampler_;
    std::vector<ObjectSlot> objects_;
};
void validateTemporalRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
void validateSceneEffects(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory);
void validateForwardRendering(std::shared_ptr<rhi::GraphicsDevice>, const std::string& shaderDirectory);
}
