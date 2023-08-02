#include<glad/glad.h>
#include<memory>
#include<assert.h>
//#include<utility>
#include"renderer/Material.h"
#include"utils/Shader.h"
#include"renderer/Texture.h"
#include"system/ResourceManager.h"
#include"utils/Shader.h"
#include"yaml-cpp/yaml.h"
#include<stb/stb_image.h>

extern std::unique_ptr<ResourceManager> resourceManager;

Material::Material() {
	hasSubSurface = false;
	initDone= false;
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
	material->initDone= true;
	return material;
}

std::shared_ptr<Material> Material::loadTerrain(const std::string& folder){
	auto material = std::make_shared<Material>(); 
	material->addTexture(resourceManager->getResource(folder + "heightMap.png"),"heightMap")
		->addTexture(resourceManager->getResource(folder + "normalMap.png"),"heightMap");
	material->initDone= true;
	return material;
}

std::shared_ptr<Material> Material::addTexture(std::shared_ptr<Texture> tex,std::string type) {
	//textures.push_back(tex);
	this->textures.insert({ type,tex });
	//this->initDone= true;
	return shared_from_this();
}

std::shared_ptr<Material> Material::addTextureAsync(std::string tex_path, std::string type) {
	auto&& tex = resourceManager->getResourceAsync(tex_path);
	this->textures.insert({ type,tex });
	//this->initDone= true;
	return shared_from_this();
}

std::shared_ptr<Material> Material::addTexture(std::string tex_path, std::string type) {
	auto&& tex = resourceManager->getResource(tex_path);
	this->textures.insert({ type,tex });
	//this->initDone= true;
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
	mat->initDone= true;
	return mat;
}

std::shared_ptr<Material> Material::loadModel(const std::string& file)
{
	auto material = std::make_shared<Material>();
	//auto mat = YAML::LoadFile(file)["Material"]["m_SavedProperties"]["m_TexEnvs"];
	//std::string guid;
	//if (mat[5]["_MainTex"]["m_Texture"].size() == 3)
	//{
	//	guid = mat[5]["_MainTex"]["m_Texture"]["guid"].as<std::string>();
	//	material->addTexture(resourceManager->guidMap[guid], "material.albedo");
	//}
	//if (mat[7]["_OcclusionMap"]["m_Texture"].size() == 3)
	//{
	//	guid = mat[7]["_OcclusionMap"]["m_Texture"]["guid"].as<std::string>();
	//	material->addTexture(resourceManager->guidMap[guid], "material.ao");
	//}
	//if (mat[6]["_MetallicGlossMap"]["m_Texture"].size() == 3)
	//{
	//	guid = mat[6]["_MetallicGlossMap"]["m_Texture"]["guid"].as<std::string>();
	//	material->addTexture(resourceManager->guidMap[guid], "material.metallic");
	//}
	//if (mat[0]["_BumpMap"]["m_Texture"].size() == 3)
	//{
	//	guid = mat[0]["_BumpMap"]["m_Texture"]["guid"].as<std::string>();
	//	material->addTexture(resourceManager->guidMap[guid], "material.normal");
	//}
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
	material->initDone= true;
	return material;
}

void Material::loadFromJson(json& data) {
	if (data.find("textures") != data.end()) {
		auto& mat = data["textures"];
		for (auto iter = mat.begin(); iter != mat.end(); ++iter) {
			auto& mat_type = iter.key();
			auto&& mat_path = iter.value().get<std::string>();
			this->addTextureAsync(mat_path, mat_type);
			//texture_path.insert({ mat_type,mat_path });
		}
		initDone= false;
	}
	if (data.find("hasSubSurface") != data.end()) {
		this->hasSubSurface = data["hasSubSurface"].get<bool>();
	}
}

/// <summary>
/// for async loading: texture data are asynchornously loaded into material::data
/// in this function, a coresponding texture object is generated and holds the texture data
/// this process is necessary because it is not allowed to operate Opengl objects in multi-thread style
/// </summary>
void Material::genTexture() {
	if (!initDone) {
		initDone= true;
		/*for (auto iter = texture_path.begin(); iter != texture_path.end(); ++iter) {
			this->addTexture(iter->second,iter->first);
		}*/
		// ------------- not working (deprecated) 
		for (auto iter = textures.begin(); iter != textures.end(); ++iter) {
			auto& tex = iter->second;
			if (!tex->id && tex->data != nullptr) {
				glGenTextures(1, &tex->id);
				//glActiveTexture(GL_TEXTURE0);
				if (tex->id == 0) {
					std::cout << "Error: texture id is 0" << '\n';
				}
				glBindTexture(GL_TEXTURE_2D, tex->id); // all upcoming GL_TEXTURE_2D operations now have effect on this texture object
				// set the texture wrapping parameters
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	// set texture wrapping to GL_REPEAT (default wrapping method)
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
				// set texture filtering parameters
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				
				if (tex->internalformat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT){
					// compressed texture
					size_t mip;
					unsigned int mipWidth = tex->width;
					unsigned int mipHeight = tex->height;
					unsigned int mipSize;
					size_t blockSize = 8;
					size_t offset = 0;
					for (mip = 0; mip < tex->num_mipmaps; ++mip){
						mipSize = ((mipWidth + 3) / 4) * ((mipHeight + 3) / 4) * blockSize;
		
						glCompressedTexImage2DARB(GL_TEXTURE_2D, mip, tex->internalformat,
							mipWidth, mipHeight, 0, mipSize,
							tex->data + offset);

						mipWidth = std::max(mipWidth >> 1, 1u);
						mipHeight = std::max(mipHeight >> 1, 1u);

						offset += mipSize;
					}
					free(tex->data);
				}
				else {
					// normal texture
					glTexImage2D(GL_TEXTURE_2D, 0, tex->internalformat, tex->width, tex->height, 0, tex->format, GL_UNSIGNED_BYTE, tex->data);
					glGenerateMipmap(GL_TEXTURE_2D);
					stbi_image_free(tex->data);
					tex->data = nullptr;
				}
				glBindTexture(GL_TEXTURE_2D, 0);

				// free data
			}
		}
	}
}

void Material::genTextureFloat() {
	if (!initDone) {
		initDone= true;
		/*for (auto iter = texture_path.begin(); iter != texture_path.end(); ++iter) {
			this->addTexture(iter->second,iter->first);
		}*/
		// ------------- not working (deprecated) 
		for (auto iter = textures.begin(); iter != textures.end(); ++iter) {
			auto& tex = iter->second;
			if (!tex->id && tex->data != nullptr) {
				glGenTextures(1, &tex->id);
				//glActiveTexture(GL_TEXTURE0);
				if (tex->id == 0) {
					std::cout << "Error: texture id is 0" << '\n';
				}
				glBindTexture(GL_TEXTURE_2D, tex->id); // all upcoming GL_TEXTURE_2D operations now have effect on this texture object
				// set the texture wrapping parameters
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	// set texture wrapping to GL_REPEAT (default wrapping method)
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
				// set texture filtering parameters
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				
				assert(tex->channels == 1);
				float* float_data = new float[tex->width * tex->height];
				for (int i = 0; i < tex->width * tex->height;++i) {
					float_data[i] = (float)tex->data[i] / 255;
				}
				glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, tex->width, tex->height, 0, GL_RED, GL_FLOAT, float_data);
				glGenerateMipmap(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);

				// free data
				delete[] float_data;
				stbi_image_free(tex->data);
				tex->data = nullptr;
			}
		}
	}
}
