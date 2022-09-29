#pragma once
#include<map>
#include<vector>
#include<string>
#include<memory>
#include"../utils/Shader.h"

class Texture;

enum class MatType {
	PBR,
	TERRAIN
};

class Material:public std::enable_shared_from_this<Material> {
public:
	Material()=default;
	Material(std::vector<std::shared_ptr<Texture>>& textures) {
		this->textures = textures;
	}
	~Material();

	std::shared_ptr<Material> addTexture(std::shared_ptr<Texture> tex);
	static std::shared_ptr<Material> loadPBR(const std::string& folder);
	static std::shared_ptr<Material> loadTerrain(const std::string& folder); 
	static std::shared_ptr<Material> loadCubeMap(const std::string& folder);

public:
	//std::shared_ptr<Shader> 
	std::vector<std::shared_ptr<Texture>> textures;
};
