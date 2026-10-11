#pragma once
#include "renderer/rhi/Resources.h"
#include <glm/glm.hpp>
#include <map>
#include "component/OceanSeaState.h"
namespace render {
struct OceanSettings {
    uint32_t size=1024;
    float length=512, amplitude=.0005f, windSpeed=30, choppiness=.8f, heightScale=1, foamScale=2, foamThreshold=.86f;
    glm::vec2 windDirection{1,1};
    int32_t seed=1337;
    // Optional radial wavelength band and ensemble RMS height, in metres.
    // Zero bounds/RMS retain the original Phillips amplitude behavior.
    float minWavelength=0,maxWavelength=0,targetRmsHeight=0;
    // Runtime blend of spectral amplitudes at fixed Gaussian seed/phase.
    bool windTransition=false;
    glm::vec2 previousWind{0};
    float windBlend=1;
    OceanSeaState seaState;
};
// Owns the full spectral -> 2D inverse FFT -> displacement/normal/foam path.
// No legacy Shader, ImageTexture or GL identifiers participate in this class.
class GpuOcean {
public:
    GpuOcean(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory,const OceanSettings&);
    ~GpuOcean();
    // Optional, lazy slope statistics for raster specular filtering. Raw FFT
    // normals and PT captures remain unchanged. Rebuilt after each simulation.
    void buildSlopeMoments();
    rhi::TextureViewHandle slopeMoments() const;
    rhi::TextureHandle slopeMomentTexture() const;
    uint32_t slopeMomentLevels() const;
    void simulate(float seconds,const OceanSettings&);
    rhi::TextureHandle displacement() const { return textures_[5]; }
    rhi::TextureHandle normal() const { return textures_[6]; }
    rhi::TextureHandle foam() const { return textures_[7]; }
    uint64_t initialSpectrumBuilds() const { return initialSpectrumBuilds_; }
    std::vector<float> readGaussian();
    std::vector<float> readHeight();
    std::vector<float> readDisplacement();
    std::vector<float> readNormal();
    std::vector<float> readFoam();
    // Numerical validation of the same 2D FFT kernels used by simulation.
    std::vector<float> inverseFFT(const std::vector<float>& complexRGBA);
private:
    struct Kernel { rhi::ComputePipelineHandle pipeline;std::vector<rhi::BindingLayout> layouts; };
    struct alignas(16) Parameters { glm::ivec4 grid;glm::vec4 spectrum,wind,displacement,previousWind,seaState,spreading,swell; };
    void record(Resources&,rhi::CommandList&,const char*,const Parameters&,const std::map<uint32_t,rhi::TextureViewHandle>&);
    void recordFFT(Resources&,rhi::CommandList&,Parameters,rhi::TextureHandle&,rhi::TextureViewHandle&);
    Resources resources_;
    struct SlopeMoments;
    std::unique_ptr<SlopeMoments> slopeMoments_;
    std::string directory_;
    bool slopeMomentsValid_=false;
    OceanSettings initial_;
    std::array<rhi::TextureHandle,9> textures_;
    std::array<rhi::TextureViewHandle,9> views_;
    std::map<std::string,Kernel> kernels_;
    Parameters spectrumKey_{};
    uint64_t initialSpectrumBuilds_=0;
    bool spectrumCached_=false;
    bool sharedFFT_=false,seeded_=false;
    int32_t seed_=0;
    std::array<bool,2> amplitudeCached_{};
    std::array<OceanSettings,2> amplitudeSettings_;
    std::array<float,2> normalizedAmplitude_{};
    std::array<bool,3> seaCached_{};
    std::array<OceanSettings,3> seaSettings_;
    std::array<float,3> seaAmplitude_{};
    float seaAmplitude(const OceanSettings&,size_t component);
    float spectralAmplitude(const OceanSettings&,size_t endpoint);
};
void validateOceanRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string& directory);
}
