#pragma once
#include<unordered_map>
#include<vector>
#include<string>
#include<memory>
#include<optional>
#include<glm/glm.hpp>
#include<json/json.hpp>
using json = nlohmann::json;

class Shader;
class Texture;

//enum class MatType {
//	PBR,
//	TERRAIN
//};

class Material:public std::enable_shared_from_this<Material> {
public:
	Material();
	//Material(std::vector<std::shared_ptr<Texture>>& textures) {
		//this->textures = textures;
	//}
	~Material();

	std::shared_ptr<Material> addTexture(std::shared_ptr<Texture> tex,std::string type);
	std::shared_ptr<Material> addTexture(std::string tex_path, std::string type);
	std::shared_ptr<Material> addTextureAsync(std::string tex_path, std::string type);

	static std::shared_ptr<Material> loadPBR(const std::string& folder);
	static std::shared_ptr<Material> loadTerrain(const std::string& folder); 
	static std::shared_ptr<Material> loadCubeMap(const std::string& folder);
	static std::shared_ptr<Material> loadModel(const std::string& file);
	static std::shared_ptr<Material> loadCustomModel(const std::string& folder);

	void loadFromJson(json& data);
	void genTexture();
	void genTextureFloat();
public:
	//std::string type; 
	bool hasSubSurface;
	float alphaCutoff = 0.0f;
	bool twoSided = false;
	glm::vec3 albedoFactor = glm::vec3(1.0f);
    std::optional<float> metallicFactor, roughnessFactor;
    float occlusionStrength = 1, normalStrength = 1, opacityFactor = 1;
    glm::vec3 emissiveFactor{0};
	std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
	std::unordered_map<std::string, std::string> texture_path;
	bool initDone;
};
