#pragma once
#include<unordered_map>
#include<vector>
#include<string>
#include<memory>
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

	static std::shared_ptr<Material> loadPBR(const std::string& folder);
	static std::shared_ptr<Material> loadTerrain(const std::string& folder); 
	static std::shared_ptr<Material> loadCubeMap(const std::string& folder);
	static std::shared_ptr<Material> loadModel(const std::string& folder);

	void loadFromJson(json& data);
	void genTexture();

public:
	//std::string type; 
	bool hasSubSurface;
	std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
	std::unordered_map<std::string, std::string> texture_path;
	bool initDone;
};
