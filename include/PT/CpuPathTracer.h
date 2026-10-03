#pragma once
#include "renderer/rhi/SceneSnapshot.h"
#include <functional>
#include <memory>
#include <string>

namespace pt {
struct Random {
    explicit Random(uint64_t seed = 1);
    uint32_t bits();
    float uniform();
    uint64_t state;
};
struct EnvironmentSample { glm::vec3 direction, radiance; float pdf = 0; };
class Environment {
  public:
    // Equirectangular HDR: row zero is north; longitude zero points along -Z.
    Environment(uint32_t width, uint32_t height, std::vector<glm::vec3> pixels);
    glm::vec3 evaluate(glm::vec3 direction) const;
    float pdf(glm::vec3 direction) const;
    EnvironmentSample sample(Random &) const;
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
    float metallic = 0, roughness = .5f, opacity = 1, distance = 0;
    uint32_t primitive = UINT32_MAX;
    bool frontFace = true;
};
struct BsdfSample { glm::vec3 direction{0}, value{0}; float pdf = 0; };
glm::vec3 evaluateBsdf(const Surface &, glm::vec3 view, glm::vec3 light);
float bsdfPdf(const Surface &, glm::vec3 view, glm::vec3 light);
BsdfSample sampleBsdf(const Surface &, glm::vec3 view, Random &);
struct Options {
    uint32_t width = 640, height = 480, samples = 64, maxDepth = 8, threads = 0;
    uint64_t seed = 1;
    float exposure = 1;
};
struct Image {
    uint32_t width = 0, height = 0, samples = 0;
    std::vector<glm::vec3> radiance, albedo, normal;
    uint64_t rays = 0, nonFiniteSamples = 0;
    double seconds = 0;
};
class CpuScene {
  public:
    explicit CpuScene(const render::RenderWorldSnapshot &);
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
    size_t triangles() const;
    size_t meshCount() const;
    size_t nodeCount() const;
    size_t memoryBytes() const;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
Image render(const CpuScene &, const Options &, const std::function<void(const Image &)> &progress = {});
void writeImage(const Image &, float exposure, const std::string &prefix);
void writeReport(const Image &, const CpuScene &, const Options &, const std::string &prefix,
                 const std::string &sceneName);
int runCommandLine(int argc, char **argv);
void validatePathTracingBridge(std::shared_ptr<rhi::GraphicsDevice>);
} // namespace pt
