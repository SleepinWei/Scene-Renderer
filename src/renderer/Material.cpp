#include<glad/glad.h>
#include<memory>
//#include<utility>
#include"Material.h"
#include"../utils/Shader.h"
#include"Texture.h"
#include"../renderer/ResourceManager.h"
#include"../utils/Shader.h"
#include"../include/yaml-cpp/yaml.h"

extern std::unique_ptr<ResourceManager> resourceManager;

Material::Material() {
	hasSubSurface = false;
}
Material::~Material() {
}

std::shared_ptr<Material> Material::loadPBR(const std::string& folder) {
	auto material = std::make_shared<Material>();
	material->addTexture(resourceManager->getResource(folder + "albedo.png"),"material.albedo")
		->addTexture(resourceManager->getResource(folder + "metallic.png"),"material.metallic")
		->addTexture(resourceManager->getResource(folder + "roughness.png"),"material.roughness")
		->addTexture(resourceManager->getResource(folder + "normal.png"),"material.normal")
		->addTexture(resourceManager->getResource(folder + "ao.png"),"material.ao")
		->addTexture(resourceManager->getResource(folder + "height.png"),"material.height");
	return material;
}

std::shared_ptr<Material> Material::loadTerrain(const std::string& folder){
	auto material = std::make_shared<Material>(); 
	material->addTexture(resourceManager->getResource(folder + "heightMap.png"),"heightMap")
		->addTexture(resourceManager->getResource(folder + "normalMap.png"),"heightMap");
	return material;
}

std::shared_ptr<Material> Material::addTexture(std::shared_ptr<Texture> tex,std::string type) {
	//textures.push_back(tex);
	this->textures.insert({ type,tex });
	return shared_from_this();
}

std::shared_ptr<Material> Material::addTexture(std::string tex_path, std::string type) {
	auto& tex = resourceManager->getResource(tex_path);
	this->textures.insert({ type,tex });
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
	//tex->type = "skybox";
	std::shared_ptr<Material> mat = std::make_shared<Material>();
	mat->addTexture(tex,"skybox");
	return mat;
}

std::shared_ptr<Material> Material::loadModel(const std::string& file)
{
	auto material = std::make_shared<Material>();
	auto mat = YAML::LoadFile(file)["Material"]["m_SavedProperties"]["m_TexEnvs"];
	std::string guid;
	if (mat[5]["_MainTex"]["m_Texture"].size() == 3)
	{
		guid = mat[5]["_MainTex"]["m_Texture"]["guid"].as<std::string>();
		material->addTexture(resourceManager->guidMap[guid], "material.albedo");
	}
	if (mat[7]["_OcclusionMap"]["m_Texture"].size() == 3)
	{
		guid = mat[7]["_OcclusionMap"]["m_Texture"]["guid"].as<std::string>();
		material->addTexture(resourceManager->guidMap[guid], "material.ao");
	}
	if (mat[6]["_MetallicGlossMap"]["m_Texture"].size() == 3)
	{
		guid = mat[6]["_MetallicGlossMap"]["m_Texture"]["guid"].as<std::string>();
		material->addTexture(resourceManager->guidMap[guid], "material.metallic");
	}
	if (mat[0]["_BumpMap"]["m_Texture"].size() == 3)
	{
		guid = mat[0]["_BumpMap"]["m_Texture"]["guid"].as<std::string>();
		material->addTexture(resourceManager->guidMap[guid], "material.normal");
	}
	return material;
}

std::shared_ptr<Material> Material::loadCustomModel(const std::string& folder)
{
	auto material = std::make_shared<Material>();
	material->addTexture(folder + "albedo.png", "material.albedo")
		->addTexture(folder + "ao.png", "material.ao")
		->addTexture(folder + "metallic.png", "material.metallic")
		->addTexture(folder + "normal.png", "material.normal")
		->addTexture(folder + "roughness.png", "material.roughness");
		//->addTexture("height.png", "material.height");
	return material;
}

void Material::loadFromJson(json& data) {
	if (data.find("textures") != data.end()) {
		auto& mat = data["textures"];
		for (auto iter = mat.begin(); iter != mat.end(); ++iter) {
			auto& mat_type = iter.key();
			auto& mat_path = iter.value().get<std::string>();
			this->addTexture(mat_path, mat_type);
		}
	}
	if (data.find("hasSubSurface") != data.end()) {
		this->hasSubSurface = data["hasSubSurface"].get<bool>();
	}
}
