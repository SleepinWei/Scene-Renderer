#pragma once
#include "renderer/rhi/SceneSnapshot.h"
#include "PT/Sampler.h"
#include "PT/SceneData.h"
#include "PT/Medium.h"
#include <functional>
#include <memory>
#include <string>

namespace pt {
class PhotonMap;
struct EnvironmentSample { glm::vec3 direction, radiance; float pdf = 0; };
class Environment {
  public:
    // Equirectangular HDR: row zero is north; longitude zero points along -Z.
    Environment(uint32_t width, uint32_t height, std::vector<glm::vec3> pixels);
    glm::vec3 evaluate(glm::vec3 direction) const;
    float pdf(glm::vec3 direction) const;
    EnvironmentSample sample(Random &) const;
    const std::vector<double> &cumulative() const { return cdf_; }
    double totalWeight() const { return total_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    const std::vector<glm::vec3> &pixels() const { return pixels_; }
  private:
    uint32_t width_, height_;
    std::vector<glm::vec3> pixels_;
    std::vector<double> cdf_;
    double total_ = 0;
};
struct Surface {
    glm::vec3 position{0}, geometricNormal{0,1,0}, normal{0,1,0};
    glm::vec3 albedo{1}, emission{0};
    glm::vec3 diffuseTransmission{0};
    glm::vec3 diffuseNormal{0}, smoothNormal{0}; // Zero uses normal / disables bump shadowing for synthetic surfaces.
    glm::vec2 uv{0};
    float metallic = 0, roughness = .5f, opacity = 1, distance = 0, ior = 0;
    uint32_t bsdfModel = 0;
    bool twoSided = false;
    bool water = false;
    bool thinDielectric = false; // Smooth parallel sheet; no medium transition.
    float foam = 0;
    glm::vec3 absorption{0};
    float exteriorIor=1;
    float transmissionRoughness=0;
    uint32_t mediumId=0;
    uint32_t primitive = UINT32_MAX;
    bool frontFace = true;
};
enum class TransportMode {Radiance, Importance};
struct DielectricMaterial {uint64_t objectId;float ior=1.5f;};
struct MediumInfo {uint32_t id=0,kind=0;float ior=1;Medium volume;float roughness=0;};
struct EmitterSample {Surface surface;float pdfArea=0;};
struct BsdfSample { glm::vec3 direction{0}, value{0}; float pdf = 0; bool delta=false, transmission=false; };
float dielectricFresnel(float cosine,float etaI,float etaT);
float thinDielectricReflectance(float cosine,float exteriorIor,float sheetIor);
glm::vec3 correctedReflectionNormal(glm::vec3 geometric,glm::vec3 view,glm::vec3 shading);
// evaluateBsdf and sample values use this cosine measure; controlled closures
// integrate each lobe normal against a shared geometric measure.
float surfaceCosine(const Surface &, glm::vec3 light);
glm::vec3 evaluateBsdf(const Surface &, glm::vec3 view, glm::vec3 light,TransportMode = TransportMode::Radiance);
float bsdfPdf(const Surface &, glm::vec3 view, glm::vec3 light);
BsdfSample sampleBsdf(const Surface &, glm::vec3 view, Random &,TransportMode = TransportMode::Radiance);
struct Options {
    uint32_t width = 640, height = 480, samples = 64, maxDepth = 8, threads = 0;
    uint64_t seed = 1;
    float exposure = 1;
    bool sobol = true, adaptive = true, waterSunProposal = true, thinSunProposal = true;
    uint32_t minimumSamples = 64;
    float relativeError = .03f, absoluteError = .0005f;
    bool guiding = false, radianceCache = false, bdpt = false;
    bool photonMapping = false;
    uint32_t photonPaths = 200000, gpuBatchSamples = 8;
    bool shadowAnyHit = true;
    float photonRadius = .08f;
    uint32_t trainingSamples = 64, cacheMinimum = 64, cacheDepth = 2;
    float guideCellSize = 0;
    uint32_t checkpointSamples = 256; // Fixed-spp film readback/save interval; adaptive checks stay at 32 spp.
};
struct AccelerationStats {
    uint64_t uniqueMeshes=0,uniqueTriangles=0,instances=0,expandedTriangles=0,blasNodes=0,tlasNodes=0;
    uint64_t geometryBytes=0,blasBytes=0,tlasBytes=0,instanceBytes=0,emitterBytes=0;
    double geometrySeconds=0,blasSeconds=0,tlasSeconds=0,emitterSeconds=0,cameraMediaSeconds=0,totalSeconds=0;
};
struct Image {
    uint32_t width = 0, height = 0, samples = 0;
    std::vector<glm::vec3> radiance, albedo, normal, caustics;
    std::vector<uint32_t> sampleCounts;
    uint64_t rays = 0, nonFiniteSamples = 0, totalSamples = 0, volumeEvents = 0;
    uint32_t convergedPixels = 0;
    std::string execution = "CPU";
    double seconds = 0, setupSeconds = 0;
    uint64_t gpuBufferBytes = 0;
    double trainingSeconds = 0;
    double photonSeconds = 0;
    uint64_t photonRays = 0, storedPhotons = 0, causticPhotons = 0, photonBytes = 0;
    uint64_t trainingRays = 0, guideHits = 0, cacheHits = 0;
    uint32_t trainedCells = 0;
    float guideCellSize = 0;
    std::vector<glm::vec3> denoised;
    std::string denoiser, denoiseDevice;
    double denoiseSeconds = 0;
    bool denoiseAuxiliary = false;
    double sceneExportSeconds=0,dispatchSeconds=0,readbackSeconds=0,filmUpdateSeconds=0,checkpointSeconds=0;
    uint64_t dispatches=0,readbacks=0,readbackBytes=0;
};
class CpuScene {
  public:
    explicit CpuScene(const render::RenderWorldSnapshot &,const std::vector<DielectricMaterial> & = {});
    ~CpuScene();
    CpuScene(const CpuScene &) = delete;
    CpuScene &operator=(const CpuScene &) = delete;
    // Immutable acceleration/material data; environment is assigned before rendering.
    std::shared_ptr<const Environment> environment;
    glm::vec3 sunDirection{0,1,0}, sunIrradiance{0};
    float sunRadius = 0;
    bool intersect(glm::vec3 origin, glm::vec3 direction, float minimum, float maximum,
                   Surface &, bool bruteForce = false) const;
    glm::vec3 trace(glm::vec3 origin, glm::vec3 direction, Random &, uint32_t maxDepth,
                    uint64_t &rays,uint64_t *volumeEvents=nullptr,bool waterSunProposal=true,bool thinSunProposal=true,
                    const PhotonMap *photons=nullptr,bool shadowAnyHit=true,glm::vec3 *caustics=nullptr) const;
    void cameraRay(float u, float v, glm::vec3 &origin, glm::vec3 &direction) const;
    SceneData exportData() const;
    EmitterSample sampleEmitter(Random &) const;
    float emitterPdfArea(uint32_t primitive) const;
    float cameraPdf(glm::vec3 direction) const;
    bool project(glm::vec3 point,glm::vec2 &uv,float &pdf) const;
    void validateBidirectional() const;
    bool intersectsAny(glm::vec3 origin,glm::vec3 direction,float minimum,float maximum) const;
    bool intersectShadow(glm::vec3 origin,glm::vec3 direction,float minimum,float maximum,Surface &transparent,bool &opaque) const;
    bool opaqueShadows() const;
    void validatePhotonMapping() const;
    std::pair<glm::vec3,glm::vec3> bounds() const;
    size_t triangles() const;
    size_t meshCount() const;
    size_t dielectricCount() const;
    size_t nodeCount() const;
    size_t memoryBytes() const;
    AccelerationStats accelerationStats() const;
    size_t proceduralCount(uint32_t kind) const;
    float capturedTime() const;
    size_t scatteringCount() const;
    // Up to eight area-ranked world-space sheet orientations; sampling hints only.
    const std::vector<glm::vec3> &thinSolarNormals() const;
    std::vector<MediumInfo> media() const;
    std::array<uint32_t,8> initialMedia(glm::vec3 origin,std::array<uint32_t,8> *winding=nullptr) const;
    glm::vec3 initialAbsorption(glm::vec3 origin) const;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
void validateOptions(const Options &);
Image renderBdpt(const CpuScene &,const Options &,const std::function<void(const Image &)> &progress = {});
Image render(const CpuScene &, const Options &, const std::function<void(const Image &)> &progress = {});
void writeImage(const Image &, float exposure, const std::string &prefix);
void writeReport(const Image &, const CpuScene &, const Options &, const std::string &prefix,
                 const std::string &sceneName);
int runCommandLine(int argc, char **argv);
void validatePathTracingBridge(std::shared_ptr<rhi::GraphicsDevice>);
} // namespace pt
