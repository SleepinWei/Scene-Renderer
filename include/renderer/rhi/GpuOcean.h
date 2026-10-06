#pragma once
#include "renderer/rhi/Resources.h"
#include <glm/glm.hpp>
#include <map>
namespace render {
struct OceanSettings {
    uint32_t size=1024;
    float length=512, amplitude=.0005f, windSpeed=30, choppiness=.8f, heightScale=1, foamScale=2, foamThreshold=.86f;
    glm::vec2 windDirection{1,1};
    int32_t seed=1337;
    // Optional radial wavelength band and ensemble RMS height, in metres.
    // Zero bounds/RMS retain the original Phillips amplitude behavior.
    float minWavelength=0,maxWavelength=0,targetRmsHeight=0;
};
// Owns the full spectral -> 2D inverse FFT -> displacement/normal/foam path.
// No legacy Shader, ImageTexture or GL identifiers participate in this class.
class GpuOcean {
public:
    GpuOcean(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory,const OceanSettings&);
    void simulate(float seconds,const OceanSettings&);
    rhi::TextureHandle displacement() const { return textures_[5]; }
    rhi::TextureHandle normal() const { return textures_[6]; }
    rhi::TextureHandle foam() const { return textures_[7]; }
    std::vector<float> readGaussian();
    std::vector<float> readHeight();
    std::vector<float> readDisplacement();
    std::vector<float> readNormal();
    std::vector<float> readFoam();
    // Numerical validation of the same 2D FFT kernels used by simulation.
    std::vector<float> inverseFFT(const std::vector<float>& complexRGBA);
private:
    struct Kernel { rhi::ComputePipelineHandle pipeline;std::vector<rhi::BindingLayout> layouts; };
    struct alignas(16) Parameters { glm::ivec4 grid;glm::vec4 spectrum,wind,displacement; };
    void record(Resources&,rhi::CommandList&,const char*,const Parameters&,const std::map<uint32_t,rhi::TextureViewHandle>&);
    void recordFFT(Resources&,rhi::CommandList&,Parameters,rhi::TextureHandle&,rhi::TextureViewHandle&);
    Resources resources_;
    OceanSettings initial_;
    std::array<rhi::TextureHandle,8> textures_;
    std::array<rhi::TextureViewHandle,8> views_;
    std::map<std::string,Kernel> kernels_;
    bool sharedFFT_=false,seeded_=false;
    int32_t seed_=0;
    bool amplitudeCached_=false;
    OceanSettings amplitudeSettings_;
    float normalizedAmplitude_=0;
};
void validateOceanRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string& directory);
}
