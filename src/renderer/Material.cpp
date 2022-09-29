#include<glad/glad.h>
#include"Material.h"
#include"../utils/Shader.h"
#include<utility>
#include"Texture.h"
#include<memory>
#include"../renderer/ResourceManager.h"

extern std::unique_ptr<ResourceManager> resourceManager;

Material::~Material() {
}

std::shared_ptr<Material> Material::loadPBR(const std::string& folder) {
	auto material = std::make_shared<Material>();
	material->addTexture(resourceManager->getResource(folder + "albedo.png"))
		->addTexture(resourceManager->getResource(folder + "metallic.png"))
		->addTexture(resourceManager->getResource(folder + "roughness.png"))
		->addTexture(resourceManager->getResource(folder + "normal.png"))
		->addTexture(resourceManager->getResource(folder + "ao.png"));
	return material;
}

std::shared_ptr<Material> Material::loadTerrain(const std::string& folder){
	auto material = std::make_shared<Material>(); 
	material->addTexture(resourceManager->getResource(folder + "heightMap.png"))
		->addTexture(resourceManager->getResource(folder + "normalMap.png"));
	return material;
}

std::shared_ptr<Material> Material::addTexture(std::shared_ptr<Texture> tex) {
	textures.push_back(tex);
	return shared_from_this();
}


std::shared_ptr<Material> Material::loadCubeMap(const std::string& folder_path) {
	std:: vector<std::string> faces
	{
		folder_path + "right.jpg",
		folder_path + "left.jpg",
		folder_path + "top.jpg",
		folder_path + "bottom.jpg",
		folder_path + "front.jpg",
		folder_path + "back.jpg"
	};

	unsigned int textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

	int width, height, nrChannels;
	for (unsigned int i = 0; i < faces.size(); i++)
	{
		unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
		if (data)
		{
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
				0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data
			);
			stbi_image_free(data);
		}
		else
		{
			std::cout << "Cubemap tex failed to load at path: " << faces[i] << std::endl;
			stbi_image_free(data);
		}
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	std::shared_ptr<Texture> tex = std::make_shared<Texture>();
	tex->name = folder_path; 
	tex->id = textureID; 
	tex->type = "skybox";
	std::shared_ptr<Material> mat = std::make_shared<Material>();
	mat->addTexture(tex);
	return mat;
}
