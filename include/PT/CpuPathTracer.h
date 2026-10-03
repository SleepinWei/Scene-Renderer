#pragma once
#include "renderer/rhi/SceneSnapshot.h"
#include "PT/Sampler.h"
#include "PT/SceneData.h"
#include <functional>
#include <memory>
#include <string>

namespace pt {
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
    glm::vec2 uv{0};
    float metallic = 0, roughness = .5f, opacity = 1, distance = 0, ior = 0;
    bool twoSided = false;
    uint32_t primitive = UINT32_MAX;
    bool frontFace = true;
};
enum class TransportMode {Radiance, Importance};
struct DielectricMaterial {uint64_t objectId;float ior=1.5f;};
struct EmitterSample {Surface surface;float pdfArea=0;};
struct BsdfSample { glm::vec3 direction{0}, value{0}; float pdf = 0; bool delta=false; };
float dielectricFresnel(float cosine,float etaI,float etaT);
glm::vec3 evaluateBsdf(const Surface &, glm::vec3 view, glm::vec3 light);
float bsdfPdf(const Surface &, glm::vec3 view, glm::vec3 light);
BsdfSample sampleBsdf(const Surface &, glm::vec3 view, Random &,TransportMode = TransportMode::Radiance);
struct Options {
    uint32_t width = 640, height = 480, samples = 64, maxDepth = 8, threads = 0;
    uint64_t seed = 1;
    float exposure = 1;
    bool sobol = true, adaptive = true;
    uint32_t minimumSamples = 64;
    float relativeError = .03f, absoluteError = .0005f;
    bool guiding = false, radianceCache = false, bdpt = false;
    uint32_t trainingSamples = 64, cacheMinimum = 64, cacheDepth = 2;
    float guideCellSize = 0;
};
struct Image {
    uint32_t width = 0, height = 0, samples = 0;
    std::vector<glm::vec3> radiance, albedo, normal, caustics;
    std::vector<uint32_t> sampleCounts;
    uint64_t rays = 0, nonFiniteSamples = 0, totalSamples = 0;
    uint32_t convergedPixels = 0;
    std::string execution = "CPU";
    double seconds = 0, setupSeconds = 0;
    uint64_t gpuBufferBytes = 0;
    double trainingSeconds = 0;
    uint64_t trainingRays = 0, guideHits = 0, cacheHits = 0;
    uint32_t trainedCells = 0;
    float guideCellSize = 0;
    std::vector<glm::vec3> denoised;
    std::string denoiser, denoiseDevice;
    double denoiseSeconds = 0;
    bool denoiseAuxiliary = false;
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
                    uint64_t &rays) const;
    void cameraRay(float u, float v, glm::vec3 &origin, glm::vec3 &direction) const;
    SceneData exportData() const;
    EmitterSample sampleEmitter(Random &) const;
    float emitterPdfArea(uint32_t primitive) const;
    float cameraPdf(glm::vec3 direction) const;
    bool project(glm::vec3 point,glm::vec2 &uv,float &pdf) const;
    void validateBidirectional() const;
    size_t triangles() const;
    size_t meshCount() const;
    size_t dielectricCount() const;
    size_t nodeCount() const;
    size_t memoryBytes() const;
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
