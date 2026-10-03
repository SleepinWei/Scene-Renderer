#pragma once
#include "engine/LogicAsset.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <glm/glm.hpp>
#include <json/json.hpp>
using json = nlohmann::json;
class Shader;
class Texture;
struct MaterialProperties {
    bool hasSubSurface = false;
    float alphaCutoff = 0;
    bool twoSided = false;
    glm::vec3 albedoFactor = glm::vec3(1);
    std::optional<float> metallicFactor = {};
    std::optional<float> roughnessFactor = {};
    float occlusionStrength = 1;
    float normalStrength = 1;
    float opacityFactor = 1;
    glm::vec3 emissiveFactor = glm::vec3(0);
};
struct MaterialData : MaterialProperties {
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
    std::unordered_map<std::string, std::string> texture_path;
    bool initDone = false;
};
class Material : public engine::LogicAsset, public std::enable_shared_from_this<Material> {
  public:
    Material();
    Material(const Material &);
    ~Material();
    MaterialProperties properties() const;
    MaterialData snapshot() const;
    void setProperties(const MaterialProperties &);
    uint64_t parameterRevision() const {
        checkLogicThread();
        return parameterRevision_;
    }
    bool getHasSubSurface() const {
        checkLogicThread();
        return hasSubSurface;
    }
    void setHasSubSurface(bool value) {
        auto copy = properties();
        copy.hasSubSurface = value;
        setProperties(copy);
    }
    float getAlphaCutoff() const {
        checkLogicThread();
        return alphaCutoff;
    }
    void setAlphaCutoff(float value) {
        auto copy = properties();
        copy.alphaCutoff = value;
        setProperties(copy);
    }
    bool getTwoSided() const {
        checkLogicThread();
        return twoSided;
    }
    void setTwoSided(bool value) {
        auto copy = properties();
        copy.twoSided = value;
        setProperties(copy);
    }
    glm::vec3 getAlbedoFactor() const {
        checkLogicThread();
        return albedoFactor;
    }
    void setAlbedoFactor(glm::vec3 value) {
        auto copy = properties();
        copy.albedoFactor = value;
        setProperties(copy);
    }
    std::optional<float> getMetallicFactor() const {
        checkLogicThread();
        return metallicFactor;
    }
    void setMetallicFactor(std::optional<float> value) {
        auto copy = properties();
        copy.metallicFactor = value;
        setProperties(copy);
    }
    std::optional<float> getRoughnessFactor() const {
        checkLogicThread();
        return roughnessFactor;
    }
    void setRoughnessFactor(std::optional<float> value) {
        auto copy = properties();
        copy.roughnessFactor = value;
        setProperties(copy);
    }
    float getOcclusionStrength() const {
        checkLogicThread();
        return occlusionStrength;
    }
    void setOcclusionStrength(float value) {
        auto copy = properties();
        copy.occlusionStrength = value;
        setProperties(copy);
    }
    float getNormalStrength() const {
        checkLogicThread();
        return normalStrength;
    }
    void setNormalStrength(float value) {
        auto copy = properties();
        copy.normalStrength = value;
        setProperties(copy);
    }
    float getOpacityFactor() const {
        checkLogicThread();
        return opacityFactor;
    }
    void setOpacityFactor(float value) {
        auto copy = properties();
        copy.opacityFactor = value;
        setProperties(copy);
    }
    glm::vec3 getEmissiveFactor() const {
        checkLogicThread();
        return emissiveFactor;
    }
    void setEmissiveFactor(glm::vec3 value) {
        auto copy = properties();
        copy.emissiveFactor = value;
        setProperties(copy);
    }
    const auto &getTextures() const {
        checkLogicThread();
        return textures;
    }
    const auto &getTexturePaths() const {
        checkLogicThread();
        return texture_path;
    }
    bool isInitialized() const {
        checkLogicThread();
        return initDone;
    }
    void setInitialized(bool value) {
        checkLogicThread();
        initDone = value;
    }
    void setTexturePath(std::string type, std::string path);
    void setTextures(std::unordered_map<std::string, std::shared_ptr<Texture>>);
    bool removeTexture(const std::string &type);
    std::shared_ptr<Material> addTexture(std::shared_ptr<Texture>, std::string);
    std::shared_ptr<Material> addTexture(std::string, std::string);
    std::shared_ptr<Material> addTextureAsync(std::string, std::string);
    static std::shared_ptr<Material> loadPBR(const std::string &);
    static std::shared_ptr<Material> loadTerrain(const std::string &);
    static std::shared_ptr<Material> loadCubeMap(const std::string &);
    static std::shared_ptr<Material> loadModel(const std::string &);
    static std::shared_ptr<Material> loadCustomModel(const std::string &);
    void loadFromJson(json &);
    void genTexture();
    void genTextureFloat();

  private:
    static void validateProperties(const MaterialProperties &);
    bool hasSubSurface = false;
    float alphaCutoff = 0;
    bool twoSided = false;
    glm::vec3 albedoFactor = glm::vec3(1);
    std::optional<float> metallicFactor = {};
    std::optional<float> roughnessFactor = {};
    float occlusionStrength = 1;
    float normalStrength = 1;
    float opacityFactor = 1;
    glm::vec3 emissiveFactor = glm::vec3(0);
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
    std::unordered_map<std::string, std::string> texture_path;
    bool initDone = false;
    uint64_t parameterRevision_ = 1;
};
