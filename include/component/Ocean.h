#pragma once
#include "component/Component.h"
#include "buffer/ImageTexture.h"
#include "utils/Shader.h"
#include "component/ShoreWaterSettings.h"
#include <memory>
#include <vector>

class Texture;
class FrameBuffer;

struct OceanConfiguration {
    int FFTPow=10, fft_size=1024, MeshSize=513;
    float MeshLength=512, TimeScale=1;
    float SpectrumLength=0; // Zero follows MeshLength; otherwise tile this FFT domain across the grid.
    glm::vec4 WindAndSeed={1,1,0,0}; // xy wind direction; zw retained for source compatibility.
    int seed=1337;
    bool animate=true, detailWaves=true;
    bool cameraGrid=true, underwaterCapture=true, volumeIntegration=true;
    bool robustRefraction=false,multipleScattering=false;
    bool underwaterView=true,underwaterFog=true;
    bool underwaterWideRefraction=true; // Recover refracted air geometry outside the camera frustum.
    bool underwaterParticles=false;
    bool underwaterSunShafts=false;
    float sunShaftStrength=1; // Contrast of refracted solar flux in the volume, 0..3.
    int underwaterVolumeSteps=4; // Eye and submerged surface paths, 4..32.
    float particleDensity=.35f; // Occupancy of world-space suspended sediment cells.
    bool shortWaveRipples=false;
    bool bedCaustics=false;
    bool causticCascades=true,causticMeshReceivers=true;
    float causticStrength=1;
    float rippleRmsHeight=.025f; // Metres, before Small wave detail multiplier.
    uint32_t opticalDebug=0;
    ShoreWaterSettings shore;
    float gridFocus=8; // Metres: concentrates grid samples around the camera.
    float detailStrength=1;
    float A=0.0005f, Lambda=0.8f, HeightScale=1;
    float BubblesScale=2, BubblesThreshold=0.86f, WindScale=30;
    float seaLevel=-5;
    std::string waterMaskPath; // White water / black land, terrain material UV orientation.
    bool refraction=true;
    float refractionStrength=1, deepWaterDistance=40, subsurfaceStrength=1;
    glm::vec3 absorption={.12f,.04f,.02f}; // RGB absorption coefficient, 1/metre.
    glm::vec3 scattering={.025f,.05f,.07f};
    float scatteringAnisotropy=.65f;
    float outer_FresnelScale=0.02f;
    glm::vec3 outer_OceanColorShallow=pow(glm::vec3(.30713776f,.4703595f,.5471698f),glm::vec3(2.2f));
    glm::vec3 outer_OceanColorDeep=pow(glm::vec3(.0499288f,.1436479f,.20754719f),glm::vec3(2.2f));
    glm::vec3 outer_BubblesColor={1,1,1};
    glm::vec3 outer_Specular=pow(glm::vec3(.3962264f,.3943574f,.3943574f),glm::vec3(2.2f));
    int outer_Gloss=256;
    glm::vec3 outer_ambient={0,0,0};
};
// Periodic Tessendorf surface: meters, seconds, wind speed in m/s.
class Ocean : private OceanConfiguration, public Component, public std::enable_shared_from_this<Ocean> {
public:
    Ocean();
    ~Ocean();
    void render();
    OceanConfiguration settings() const {checkLogicThread();return *this;}
    void setSettings(OceanConfiguration);
    template<class F> void updateSettings(F&& edit) {auto candidate=settings();edit(candidate);setSettings(candidate);}
    void simulate(float seconds); // Deterministic compute-only entry for numerical validation.
    std::shared_ptr<ImageTexture> GaussianRandomRT_Texture, HeightSpectrumRT_Texture;
    std::shared_ptr<ImageTexture> DisplaceXSpectrumRT_Texture, DisplaceZSpectrumRT_Texture;
    std::shared_ptr<ImageTexture> InputRT_Texture, OutputRT_Texture, DisplaceRT_Texture;
    std::shared_ptr<ImageTexture> NormalRT_Texture, BubblesRT_Texture;
    std::shared_ptr<Shader> GaussianRandomRT_Shader, DisplaceSpectrum_Shader, HeightSpectrum_Shader;
    std::shared_ptr<Shader> FFTHorizontal_Shader, FFTHorizontalEnd_Shader;
    std::shared_ptr<Shader> FFTVertical_Shader, FFTVerticalEnd_Shader;
    std::shared_ptr<Shader> TextureDisplace_Shader, TextureNormalBubbles_Shader, draw_shader;
private:
    float inner_time=0, deltaTime=0, lastFrame=0;
    std::vector<unsigned> vertexIndexs;
    std::vector<float> vertexInfo;
    unsigned VAO=0,VBO=0,EBO=0;
    bool initDone=false;
    int initializedSize=0, initializedMeshSize=0, initializedSeed=0;
    float initializedLength=0;
    void Start();
    void Update();
    void Draw();
    std::shared_ptr<Ocean> detailOcean;
    std::shared_ptr<Texture> previousDisplacement,previousDetailDisplacement;
    std::shared_ptr<Shader> copyDisplacementShader;
    float previousSeaLevel=0;
    std::shared_ptr<Texture> opaqueSceneColor;
    std::shared_ptr<FrameBuffer> opaqueSceneBuffer;
    void initTextures();
    void initMesh();
    void initShaders();
    void initGaussianRandom();
    void ComputeFFT(std::shared_ptr<Shader> shader,std::shared_ptr<ImageTexture> input);
    void ComputeOceanValue();
};
