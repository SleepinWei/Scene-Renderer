#pragma once
#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <memory>
#include <glm/glm.hpp>
#include "component/Component.h"
#include "engine/RenderSettings.h"
#include "utils/header.h"

class Material;
class MeshFilter;
class Texture;
class Shader;

class MeshRenderer : public Component, public std::enable_shared_from_this<MeshRenderer> {
  public:
    MeshRenderer();
    ~MeshRenderer();

    void render(const std::shared_ptr<Shader> &shader);

    // Custom programs are only supported by the historical OpenGL rendering path.
    std::shared_ptr<MeshRenderer> setLegacyShader(std::shared_ptr<Shader> value);
    std::shared_ptr<MeshRenderer> setShader(ShaderType type);
    std::shared_ptr<MeshRenderer> setShader(std::string type);

    std::shared_ptr<MeshRenderer> setDrawMode(GLenum drawMode_);
    std::shared_ptr<MeshRenderer> setPolyMode(GLenum polyMode_);

    virtual void loadFromJson(json &data);

  public:
    ShaderType getShaderType() const {
        checkLogicThread();
        return shaderType;
    }
    GLenum getDrawMode() const {
        checkLogicThread();
        return drawMode;
    }
    GLenum getPolyMode() const {
        checkLogicThread();
        return polyMode;
    }
    std::shared_ptr<Shader> getShader() const {
        checkLogicThread();
        return shader;
    }

  private:
    ShaderType shaderType = ShaderType::PBR;
    GLenum drawMode;
    GLenum polyMode;

    std::shared_ptr<Shader> shader;
};
