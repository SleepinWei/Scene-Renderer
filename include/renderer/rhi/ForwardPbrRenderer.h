#pragma once
#include "renderer/rhi/TreeWind.h"
namespace pt {class GpuProbeTracer;}
#include "renderer/rhi/GpuMesh.h"
#include "renderer/rhi/GpuMaterial.h"
#include "renderer/rhi/GpuAtmosphere.h"
#include "renderer/rhi/OceanSurface.h"
#include "renderer/rhi/GpuTemporal.h"
#include "renderer/rhi/GpuPostProcessor.h"
#include "component/CloudSettings.h"
#include "renderer/rhi/WeatherLighting.h"
#include "renderer/rhi/DdgiSettings.h"
#include "renderer/rhi/ClusterGeometry.h"
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
    bool pcss=true,instanceCulling=true;
    float distance=300, cascadeBlend=.1f, depthBias=.002f;
    float sunAngularRadius=.00465f, localLightRadius=.05f, maxFilterTexels=24;
};
struct FrameData {
    glm::mat4 viewProjection{1}; // NDC depth 0..1.
    glm::mat4 view{1};
    float nearPlane=.1f,farPlane=100;
    glm::vec3 cameraPosition{0,0,3};
    float ambient = .03f;
    float skySpecularScale = 1; // Controlled environment-lighting ablation; diffuse sky is independent.
    bool shadows=false,ssao=false,rsm=false,sky=false,taa=false;
    bool taaHighQuality=true; // Bicubic history, ray-box clipping, conservative depth validation.
    glm::vec2 temporalJitter{0}; // Internal raster offset in pixels; standalone resolve inputs default to unjittered.
    uint64_t historyKey=0;
    uint64_t giSceneKey=0; // Stable scene identity, independent of temporal lighting / geometry revisions.
    uint32_t viewportWidth=1280,viewportHeight=720;
    float aoRadius=1,aoBias=.025f,aoPower=1.5f;
    bool aoHorizon=true,aoDenoise=true;
    bool aoLinearDepth=false,aoDepthHierarchy=false; // Native AO diagnostics / quality gates.
    int aoSlices=4,aoSteps=4;
    bool toneMapping=true,forwardShading=false;
    bool graphCulling=true; // Dead-pass elimination; disable for a recording baseline.
    bool objectCulling=true,triangleCulling=false; // GPU triangle path is opt-in until scene profiling proves a net win.
    uint32_t triangleCullMinimum=2048; // Avoid three compute dispatches for small meshes.
    bool hizCulling=false; // Current-frame opaque occluder depth; opt in after scene profiling.
    float hizOccluderMinArea=.04f; // Fraction of viewport covered by projected bounds.
    uint32_t hizMaxOccluders=32,hizOccluderTriangleBudget=250000;
    ClusterSettings clusters;
    PostProcessSettings postProcess;
    DdgiSettings ddgi;
    RsmSettings rsmSettings;
    ShadowSettings shadowSettings;
    bool directionalEnabled=true;
    bool inverseSquareLocalLights=false; // Legacy forward validation can retain constant point attenuation.
    AtmosphereSettings atmosphere;float sunAngle=10,sunAzimuth=0,seaLevelMeters=0,multipleScattering=1,groundAlbedo=.2f,timeSeconds=0;
    TreeWindSettings treeWind;
    CloudSettings clouds;
    WeatherLightingSettings weather;
    float rainMetersPerSecond=0;
    glm::vec2 rainWind{0};
    bool rainVisible=true;
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
    std::shared_ptr<GpuClusterGeometry> clusters;
};
enum class PbrPath { Forward, Deferred, Scene };
class ShadowRenderer;
class GpuClouds;
class GpuRain;
class GpuAirFog;
class GpuDdgi;
class GpuGeometryCuller;
class GpuHiZCuller;
class GpuClusterCuller;
struct TriangleCullCounts {size_t packet=0;uint32_t inputTriangles=0,outputTriangles=0;};
struct ShadowParameters;
struct AoPassTimings {
    bool supported=false,fastReconstruction=false;
    uint32_t warmup=0,repetitions=1;
    std::vector<double> depthMilliseconds,rawMilliseconds,filterMilliseconds;
    std::string scope;
};
struct GraphPassStats {std::vector<std::string> executed,culled;};
struct GeometryCullStats {
    uint64_t sourcePackets=0,visiblePackets=0,culledPackets=0,triangleCandidates=0;
    size_t triangleDraws=0,bufferBytes=0;
    size_t triangleCacheHits=0,triangleDispatches=0;
};
struct HiZCullStats {
    bool active=false;
    size_t candidates=0,occluders=0,prepassTriangles=0,pyramidLevels=0,bufferBytes=0;
};
struct HiZCullCount {size_t packet=0;uint32_t inputTriangles=0,outputTriangles=0;bool occluder=false;};
struct HiZCullBounds {size_t packet=0;glm::vec4 rectangle{0};float nearestDepth=0;bool testable=false,occluder=false;};
class ForwardPbrRenderer {
public:
    ForwardPbrRenderer(std::shared_ptr<rhi::GraphicsDevice>, const std::string& shaderDirectory, uint32_t width, uint32_t height, PbrPath path = PbrPath::Forward);
    ~ForwardPbrRenderer();
    void resize(uint32_t width, uint32_t height);
    void resetTemporal(); // Start an independent capture or comparison.
    // When packets are camera-culled, pass the full shadow / water list last.
    void render(const FrameData&, const std::vector<DrawPacket>&, float exposure = 1, float gamma = 2.2f, const std::vector<DrawPacket>* auxiliaryPackets = nullptr);
    rhi::TextureHandle output() const;
    rhi::TextureViewHandle depthView() const;
    std::vector<glm::mat4> shadowVisibilityViews() const;
    glm::mat4 renderedViewProjection() const {return renderedVP_;}
    const GraphPassStats& graphPassStats() const {return graphPassStats_;}
    const GeometryCullStats& geometryCullStats() const {return geometryCullStats_;}
    std::vector<TriangleCullCounts> readTriangleCullCounts(); // Synchronous diagnostics; never called by render().
    ClusterCullStats clusterCullStats() const;
    std::vector<ClusterCullCount> readClusterCullCounts();
    std::vector<uint32_t> readClusterIndices(size_t packet);
    std::vector<ClusterVisibility> readClusterVisibility();
    void captureClusterIntermediates(bool enabled){captureClusterIntermediates_=enabled;}
    std::vector<float> readClusterEarlyAlbedo();
    std::vector<float> readClusterEarlyDepth();
    uint32_t clusterPyramidBaseMip(bool history=false) const;
    std::vector<float> readClusterHistoryDepth(uint32_t mip=0);
    DdgiStats ddgiStats() const;
    void setDdgiProbeTracer(std::shared_ptr<pt::GpuProbeTracer>);
    std::vector<glm::vec4> readDdgiField() const; // Diagnostic only.
    std::vector<float> readDdgiCapture() const; // Six-face scratch target, diagnostic only.
    std::vector<glm::vec4> sampleDdgi(const std::vector<glm::vec4>& positions,const std::vector<glm::vec4>& normals);
    HiZCullStats hizCullStats() const;
    std::vector<HiZCullCount> readHiZCullCounts(); // Explicit GPU readback, never used to decide draws.
    std::vector<float> readHiZDepth(uint32_t mip=0); // Offline visualization only.
    std::vector<HiZCullBounds> hizCullBounds() const;
    std::vector<float> readHDR();
    std::vector<float> readDepth();
    std::vector<float> readMotion(); // Explicit diagnostic readback.
    uint32_t waveParticleCount(uint64_t ocean) const;
    std::vector<uint8_t> readOutput();
    std::vector<float> readBackDepth();
    std::vector<float> readSSAO();
    std::vector<float> readRawAO();
    std::vector<float> readAoDepth(uint32_t mip=0);
    uint32_t aoDepthLevels() const;
    // Diagnostic only: replay AO from the last successful active Scene frame.
    // Isolated native submissions include attachment/command-buffer overhead.
    AoPassTimings measureAO(uint32_t warmup=4,uint32_t samples=16,uint32_t repetitions=1);
    std::vector<float> readShadowDepth();
    std::array<uint64_t,4> readShadowInstanceStats();
    ShadowParameters shadowParameters() const; // Value snapshot for render-thread diagnostics.
    std::vector<float> readGBuffer(uint32_t attachment);
    std::vector<float> readWaterCaustics(uint64_t ocean,uint32_t cascade=0);
    std::vector<float> readWaterCapture(uint64_t ocean,bool positions=false,bool aboveWater=false);
    std::vector<float> readShoreWater(uint64_t ocean,bool foam=false);
    uint32_t shoreSubsteps(uint64_t ocean) const;
    std::vector<float> readClouds();
    std::vector<float> readCloudShadow();
    std::vector<float> readAerialPerspective(bool transmission=false);
    uint64_t cloudShadowBakes() const;
    std::vector<float> readCloudMetadata();
    std::vector<uint8_t> readCloudVoxels();
    std::vector<uint8_t> readCloudDistance();
    std::vector<float> readCloudLight();
    std::array<uint64_t,3> cloudCacheBakes() const;
    std::array<uint32_t,2> cloudTileCounts(); // Explicit diagnostic readback: active,total.
    const rhi::GraphicsDevice* owner() const { return resources_.device.get(); }
    static rhi::BindingLayout frameLayout();
private:
    GraphPassStats graphPassStats_;
    GeometryCullStats geometryCullStats_;
    std::unique_ptr<GpuGeometryCuller> geometryCuller_;
    std::unique_ptr<GpuClusterCuller> clusterCuller_;
    std::unique_ptr<GpuHiZCuller> hizCuller_,clusterHistory_;
    bool captureClusterIntermediates_=false;
    bool clusterHistoryValid_=false;glm::mat4 clusterHistoryVP_{1};
    bool aoBenchmarkReady_=false,aoProjectionFast_=false;
    uint32_t aoDepthLevelsReady_=0;
    void recordAoDepth(rhi::CommandList&);
    struct Targets;
    struct ObjectSlot { rhi::BufferHandle data;rhi::BindingSetHandle bindings;rhi::BufferHandle frameTangents; };
    Resources resources_;
    std::unique_ptr<ShadowRenderer> shadows_;
    std::unique_ptr<GpuAtmosphere> atmosphere_;
    std::unique_ptr<GpuClouds> clouds_;
    std::unique_ptr<GpuRain> rain_;
    std::unique_ptr<GpuAirFog> airFog_;
    std::unique_ptr<GpuDdgi> ddgi_;
    std::shared_ptr<pt::GpuProbeTracer> ddgiProbeTracer_;
    std::map<uint64_t,std::unique_ptr<OceanSurface>> oceans_;
    std::string directory_;
    std::unique_ptr<GpuTemporal> temporal_;
    std::unique_ptr<GpuPostProcessor> post_;
    std::unique_ptr<Targets> targets_;
    rhi::PipelineHandle motionPipeline_,motionInstanced_,backDepthPipeline_,transparentPipeline_,transparentInstanced_;
    rhi::BindingLayout motionLayout_,transparentLayout_;
    std::map<uint64_t,glm::mat4> previousModels_;
    glm::mat4 renderedVP_{1};
    std::vector<size_t> clusterPacketIndices_;
    glm::vec3 previousCamera_{0};glm::mat4 previousProjection_{1};uint64_t historyKey_=0;bool previousTaa_=false,temporalOutput_=false;
    rhi::BufferHandle skyParameters_;rhi::TextureViewHandle skyView_,irradianceView_;
    rhi::BufferHandle effects_;
    rhi::BindingSetHandle effectsBindings_;
    rhi::PipelineHandle aoDepthPipeline_,aoReducePipeline_,ssaoPipeline_,aoFilterPipeline_,sceneForward_,sceneForwardInstanced_,sceneForwardWire_,sceneForwardWireInstanced_;
    rhi::BindingLayout sceneForwardLayout_;
    rhi::BufferHandle camera_, lighting_, quad_;
    rhi::PipelineHandle forward_, instanced_,wireframe_,wireframeInstanced_, deferredPipeline_;
    PbrPath path_;
    rhi::BindingLayout geometryLayout_;
    rhi::SamplerHandle hdrSampler_,skySampler_;
    rhi::BufferHandle inactiveWetParameters_;rhi::TextureViewHandle inactiveWetView_;rhi::SamplerHandle wetSampler_;
    std::vector<ObjectSlot> objects_;
};
void validateTemporalRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
void validateSceneEffects(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory);
void validateForwardRendering(std::shared_ptr<rhi::GraphicsDevice>, const std::string& shaderDirectory);
}
