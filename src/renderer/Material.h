#pragma once
#include<map>
#include<vector>
#include<string>
#include<memory>
#include<shader/Shader.h>

class Texture;

enum class MatType {
	PBR,
	TERRAIN
};

class Material {
public:
	Material()=default;
	Material(MatType type, const std::string& folder);
	Material(std::vector<std::shared_ptr<Texture>>& textures) {
		this->textures = textures;
	}
	~Material();

	void addTexture(std::shared_ptr<Texture> tex);
	void loadPBR(const std::string& folder);
	void loadTerrain(const std::string& folder); 

public:
	//std::shared_ptr<Shader> 
	std::vector<std::shared_ptr<Texture>> textures;
};
