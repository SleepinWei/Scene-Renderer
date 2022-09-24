#include<glad/glad.h>
#include"Material.h"
#include<shader/Shader.h>
#include<utility>
#include"Texture.h"


Material::~Material() {
}

void Material::loadPBR(const std::string& folder) {
	textures.push_back(Texture::loadFromFile(folder + "albedo.png"));
	textures.push_back(Texture::loadFromFile(folder + "metallic.png"));
	textures.push_back(Texture::loadFromFile(folder + "roughness.png"));
	textures.push_back(Texture::loadFromFile(folder + "normal.png"));
	textures.push_back(Texture::loadFromFile(folder + "ao.png"));
}

void Material::loadTerrain(const std::string& folder){
	textures.push_back(Texture::loadFromFile(folder + "heightMap.png"));
	textures.push_back(Texture::loadFromFile(folder + "normalMap.png"));
}

Material::Material(MatType type, const std::string& folder) {
	switch (type)
	{
	case MatType::PBR:
		loadPBR(folder);
		break;
	case MatType::TERRAIN:
		loadPBR(folder);
		break;
	default:
		break;
	}
}

void Material::addTexture(std::shared_ptr<Texture> tex) {
	textures.push_back(tex);
}
