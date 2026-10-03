#pragma once
#include <glm/glm.hpp>
#include "utils/Shader.h"
#include <glfw/glfw3.h>
#include <vector>
#include <memory>
#include "component/Component.h"
#include <tuple>
#include <json/json.hpp>
using json = nlohmann::json;

class Texture;
using std::shared_ptr;
using std::tuple;
using std::vector;

enum class LightType { POINT, DIRECTIONAL, SPOT };
class Light : public Component, public std::enable_shared_from_this<Light> {
  public:
    Light();
    virtual ~Light();
    void setDirtyFlag(bool dirty);

  public:
    bool castsShadow() const {
        checkLogicThread();
        return castShadow;
    }
    bool isEnabled() const {
        checkLogicThread();
        return enabled;
    }
    bool isDirty() const {
        checkLogicThread();
        return dirty;
    }
    LightType lightType() const {
        checkLogicThread();
        return type;
    }
    void setEnabled(bool value) {
        checkLogicThread();
        enabled = value;
        dirty = true;
        invalidate();
    }

  protected:
    bool castShadow = true;
    bool enabled = true;
    enum LightType type = LightType::POINT;

  public:
    static const int SHADOW_WIDTH = 512;
    static const int SHADOW_HEIGHT = 512;

  protected:
    bool dirty = true; // if position of light changes, this flag turns true
    // light 是否 dirty 要交给 prepare light data 统一设置
};

// enum class POINTLIGHT {
// SINGLE,
// CUBEMAP
//};
struct PointLightData {
    glm::vec3 color;
    // glm::vec3 position;// get position from transform

    // not used
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    float constant;
    float linear;
    float quadratic;
};
class PointLight : public Light {

  public:
    PointLightData getData() const {
        checkLogicThread();
        return data_;
    }
    void setData(PointLightData);
    void setColor(glm::vec3);
    float getNear() const {
        checkLogicThread();
        return near_;
    }
    float getFar() const {
        checkLogicThread();
        return far_;
    }
    float getFov() const {
        checkLogicThread();
        return fov_;
    }
    float getAspect() const {
        checkLogicThread();
        return aspect_;
    }
    void setShadowProjection(float nearPlane, float farPlane, float fov, float aspect);
    PointLight();
    virtual ~PointLight();
    // void initVertexObject();
    tuple<glm::mat4, glm::mat4> getLightTransform(int face);
    std::shared_ptr<PointLight> setCastShadow(bool castShadow);
    virtual void loadFromJson(json &data_) override;

  public:
    // POINTLIGHT mode;

    // std::vector <glm::mat4> lightTransforms; // 6 faces, 6 view transforms (for shadow map)

    // shadow mapping texture;
    std::shared_ptr<Texture> shadowTex;

  private:
    PointLightData data_;
    float near_, far_;
    float fov_, aspect_;
};

struct DirectionLightData {
    glm::vec3 color;
    // glm::vec3 position; // get position from transform
    glm::vec3 direction;

    // not used
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
};

class DirectionLight : public Light {
  public:
    DirectionLightData getData() const {
        checkLogicThread();
        return data_;
    }
    void setData(DirectionLightData);
    void setColor(glm::vec3);
    float getNear() const {
        checkLogicThread();
        return near_;
    }
    float getFar() const {
        checkLogicThread();
        return far_;
    }
    float getOrthoWidth() const {
        checkLogicThread();
        return ortho_width_;
    }
    void setShadowProjection(float nearPlane, float farPlane, float width);

    // glm::mat4 lightView;
    // glm::mat4 lightProj;

    // shadow mapping texture
    std::shared_ptr<Texture> shadowTex;

  public:
    DirectionLight();
    ~DirectionLight();
    std::shared_ptr<DirectionLight> setDirection(const glm::vec3 &dir);
    std::shared_ptr<DirectionLight> setCastShadow(bool castShadow);
    tuple<glm::mat4, glm::mat4> getLightTransform();
    virtual void loadFromJson(json &data_) override;

  private:
    DirectionLightData data_;
    float near_, far_;
    float ortho_width_;
};

struct SpotLightData {
    glm::vec3 color;
    // glm::vec3 position; // get position from transform
    glm::vec3 direction;

    float cutOff;
    float outerCutOff;

    float constant;
    float linear;
    float quadratic;
    // not used
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
};

class SpotLight : public Light {
  public:
    SpotLightData getData() const {
        checkLogicThread();
        return data_;
    }
    void setData(SpotLightData);
    void setColor(glm::vec3);
    float getNear() const {
        checkLogicThread();
        return near_;
    }
    float getFar() const {
        checkLogicThread();
        return far_;
    }
    float getFov() const {
        checkLogicThread();
        return fov_;
    }
    float getAspect() const {
        checkLogicThread();
        return aspect_;
    }
    void setShadowProjection(float nearPlane, float farPlane, float fov, float aspect);
    void setDirection(glm::vec3);
    void setCone(float innerCos, float outerCos);

    // glm::mat4 lightView;
    // glm::mat4 lightProj;

    // shadow mapping texture
    std::shared_ptr<Texture> depthMap;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> worldPosMap;
    std::shared_ptr<Texture> fluxMap;

  public:
    SpotLight();
    ~SpotLight();
    tuple<glm::mat4, glm::mat4> getLightTransform();
    virtual void loadFromJson(json &data_) override;

  private:
    SpotLightData data_;
    float near_, far_;
    float fov_, aspect_;
};
