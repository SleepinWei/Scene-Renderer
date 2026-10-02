#pragma once
#include <glm/glm.hpp>
#include <memory>
class RenderScene;
class Texture;
class FrameBuffer;
class Shader;

// Native-resolution temporal supersampling. All history remains in linear HDR.
class TemporalAA {
public:
    void begin(const std::shared_ptr<RenderScene>& scene,bool enabled);
    unsigned resolve(unsigned currentColor,unsigned sourceFramebuffer);
    void reset();
    bool active() const {return enabled;}
    bool historyValid() const {return valid;}
    unsigned frameCount() const {return samples;}
    unsigned motionTexture() const;
    unsigned outputTexture() const;
    static glm::vec2 jitterSample(unsigned frame);
    glm::mat4 projection{1},view{1},previousVP{1},previousView{1};
    float historyWeight=.9f;
private:
    void allocate(int width,int height);
    int width=0,height=0,writeIndex=0;
    bool enabled=false,valid=false;
    unsigned samples=0;
    std::weak_ptr<RenderScene> lastScene;
    const void* lastCamera=nullptr;
    glm::vec3 lastPosition{0},lastFront{0,0,-1};
    glm::mat4 lastProjection{1},currentVP{1};
    size_t lastSignature=0;
    std::shared_ptr<Texture> color[2],depth[2],currentDepth,motion;
    std::shared_ptr<FrameBuffer> depthBuffer,motionBuffer;
    std::shared_ptr<Shader> shader;
};
