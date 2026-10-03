#include<glad/glad.h>
#include "rhi/Device.h"
#include <cmath>
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

Material::Material() {
	hasSubSurface = false;
	initDone= false;
}
Material::~Material() {
}

std::shared_ptr<Material> Material::loadPBR(const std::string& folder) {
	auto material = std::make_shared<Material>();
	material->addTexture(ResourceManager::GetInstance()->getResource(folder + "albedo.png"),"material.albedo")
		->addTexture(ResourceManager::GetInstance()->getResource(folder + "metallic.png"),"material.metallic")
		->addTexture(ResourceManager::GetInstance()->getResource(folder + "roughness.png"),"material.roughness")
		->addTexture(ResourceManager::GetInstance()->getResource(folder + "normal.png"),"material.normal")
		->addTexture(ResourceManager::GetInstance()->getResource(folder + "ao.png"),"material.ao")
		->addTexture(ResourceManager::GetInstance()->getResource(folder + "height.png"),"material.height");
	material->initDone= true;
	return material;
}

std::shared_ptr<Material> Material::loadTerrain(const std::string& folder){
	auto material = std::make_shared<Material>(); 
	material->addTexture(ResourceManager::GetInstance()->getResource(folder + "heightMap.png"),"heightMap")
		->addTexture(ResourceManager::GetInstance()->getResource(folder + "normalMap.png"),"heightMap");
	material->initDone= true;
	return material;
}

std::shared_ptr<Material> Material::addTexture(std::shared_ptr<Texture> tex,std::string type) {
    checkLogicThread();
	//textures.push_back(tex);
	if(!tex || type.empty())throw std::invalid_argument("Null texture or empty material slot");
    textures[type]=tex;texture_path.erase(type);initDone=false;invalidate();
	//this->initDone= true;
	return shared_from_this();
}

std::shared_ptr<Material> Material::addTextureAsync(std::string tex_path, std::string type) {
    checkLogicThread();
	auto&& tex = ResourceManager::GetInstance()->getResourceAsync(tex_path);
	if(!tex || type.empty())throw std::invalid_argument("Null texture or empty material slot");
    textures[type]=tex;texture_path.erase(type);initDone=false;invalidate();
	//this->initDone= true;
	return shared_from_this();
}

std::shared_ptr<Material> Material::addTexture(std::string tex_path, std::string type) {
    checkLogicThread();
	auto&& tex = ResourceManager::GetInstance()->getResource(tex_path);
	if(!tex || type.empty())throw std::invalid_argument("Null texture or empty material slot");
    textures[type]=tex;texture_path.erase(type);initDone=false;invalidate();
	//this->initDone= true;
	return shared_from_this();
}

std::shared_ptr<Material> Material::loadCubeMap(const std::string& folder_path) {
    if(rhi::usesNativeRenderer()){auto material=std::make_shared<Material>();material->texture_path["skybox"]=folder_path;return material;}
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
	//	material->addTexture(ResourceManager::GetInstance()->guidMap[guid], "material.albedo");
	//}
	//if (mat[7]["_OcclusionMap"]["m_Texture"].size() == 3)
	//{
	//	guid = mat[7]["_OcclusionMap"]["m_Texture"]["guid"].as<std::string>();
	//	material->addTexture(ResourceManager::GetInstance()->guidMap[guid], "material.ao");
	//}
	//if (mat[6]["_MetallicGlossMap"]["m_Texture"].size() == 3)
	//{
	//	guid = mat[6]["_MetallicGlossMap"]["m_Texture"]["guid"].as<std::string>();
	//	material->addTexture(ResourceManager::GetInstance()->guidMap[guid], "material.metallic");
	//}
	//if (mat[0]["_BumpMap"]["m_Texture"].size() == 3)
	//{
	//	guid = mat[0]["_BumpMap"]["m_Texture"]["guid"].as<std::string>();
	//	material->addTexture(ResourceManager::GetInstance()->guidMap[guid], "material.normal");
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
    checkLogicThread();auto candidate=properties();
    if (data.contains("metallicFactor")) candidate.metallicFactor = data["metallicFactor"].get<float>();
    if (data.contains("roughnessFactor")) candidate.roughnessFactor = data["roughnessFactor"].get<float>();
    if (data.contains("occlusionStrength")) candidate.occlusionStrength = data["occlusionStrength"].get<float>();
    if (data.contains("normalStrength")) candidate.normalStrength = data["normalStrength"].get<float>();
    if (data.contains("opacityFactor")) candidate.opacityFactor = data["opacityFactor"].get<float>();
    if (data.contains("alphaCutoff")) candidate.alphaCutoff = data["alphaCutoff"].get<float>();
    for (int i = 0; i < 3; ++i) {
        if (data.contains("albedoFactor")) candidate.albedoFactor[i] = data["albedoFactor"].at(i).get<float>();
        if (data.contains("emissiveFactor")) candidate.emissiveFactor[i] = data["emissiveFactor"].at(i).get<float>();
    }
	if (data.find("hasSubSurface") != data.end()) {
		candidate.hasSubSurface = data["hasSubSurface"].get<bool>();
	}
    validateProperties(candidate);
    auto nextTextures=textures;auto nextPaths=texture_path;
    if(data.contains("textures"))for(const auto& entry:data.at("textures").items()) {
        const auto path=entry.value().get<std::string>();
        if(entry.key().empty()||path.empty())throw std::invalid_argument("Texture path and slot must be nonempty");
        auto texture=ResourceManager::GetInstance()->getResourceAsync(path);
        if(!texture)throw std::invalid_argument("Texture load returned null");
        nextTextures[entry.key()]=std::move(texture);nextPaths.erase(entry.key());
    }
    setProperties(candidate);
    if(data.contains("textures")){textures=std::move(nextTextures);texture_path=std::move(nextPaths);initDone=false;invalidate();}
}

/// <summary>
/// for async loading: texture data are asynchornously loaded into material::data
/// in this function, a coresponding texture object is generated and holds the texture data
/// this process is necessary because it is not allowed to operate Opengl objects in multi-thread style
/// </summary>
void Material::genTexture() {
    checkLogicThread();
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
					free(tex->data);tex->data=nullptr;
				}
				else {
					// normal texture
					glTexImage2D(GL_TEXTURE_2D, 0, tex->internalformat, tex->width, tex->height, 0, tex->format, GL_UNSIGNED_BYTE, tex->data);
					glGenerateMipmap(GL_TEXTURE_2D);
					stbi_image_free(tex->data);tex->data=nullptr;
					tex->data = nullptr;
				}
				glBindTexture(GL_TEXTURE_2D, 0);

				// free data
			}
		}
	}
}

void Material::genTextureFloat() {
    checkLogicThread();
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
				stbi_image_free(tex->data);tex->data=nullptr;
				tex->data = nullptr;
			}
		}
	}
}

Material::Material(const Material& other):engine::LogicAsset(other) {
    auto copy=other.snapshot();
    hasSubSurface=copy.hasSubSurface;
    alphaCutoff=copy.alphaCutoff;
    twoSided=copy.twoSided;
    albedoFactor=copy.albedoFactor;
    metallicFactor=copy.metallicFactor;
    roughnessFactor=copy.roughnessFactor;
    occlusionStrength=copy.occlusionStrength;
    normalStrength=copy.normalStrength;
    opacityFactor=copy.opacityFactor;
    emissiveFactor=copy.emissiveFactor;
    textures=std::move(copy.textures);texture_path=std::move(copy.texture_path);initDone=copy.initDone;parameterRevision_=other.parameterRevision();
}
MaterialProperties Material::properties() const {checkLogicThread();MaterialProperties result;
    result.hasSubSurface=hasSubSurface;
    result.alphaCutoff=alphaCutoff;
    result.twoSided=twoSided;
    result.albedoFactor=albedoFactor;
    result.metallicFactor=metallicFactor;
    result.roughnessFactor=roughnessFactor;
    result.occlusionStrength=occlusionStrength;
    result.normalStrength=normalStrength;
    result.opacityFactor=opacityFactor;
    result.emissiveFactor=emissiveFactor;
    return result;
}
MaterialData Material::snapshot() const {checkLogicThread();MaterialData result;static_cast<MaterialProperties&>(result)=properties();result.textures=textures;result.texture_path=texture_path;result.initDone=initDone;return result;}
void Material::setProperties(const MaterialProperties& value) {
    checkLogicThread();
    validateProperties(value);
    hasSubSurface=value.hasSubSurface;
    alphaCutoff=value.alphaCutoff;
    twoSided=value.twoSided;
    albedoFactor=value.albedoFactor;
    metallicFactor=value.metallicFactor;
    roughnessFactor=value.roughnessFactor;
    occlusionStrength=value.occlusionStrength;
    normalStrength=value.normalStrength;
    opacityFactor=value.opacityFactor;
    emissiveFactor=value.emissiveFactor;
    ++parameterRevision_;
}
void Material::setTexturePath(std::string type,std::string path) {
    checkLogicThread();if(type.empty()||path.empty())throw std::invalid_argument("Texture path and slot must be nonempty");
    texture_path[type]=std::move(path);textures.erase(type);initDone=false;invalidate();
}
bool Material::removeTexture(const std::string& type) {
    checkLogicThread();auto removed=textures.erase(type)+texture_path.erase(type);if(removed){initDone=false;invalidate();}return removed!=0;
}

void Material::setTextures(std::unordered_map<std::string,std::shared_ptr<Texture>> value) {checkLogicThread();for(const auto& item:value)if(item.first.empty()||!item.second)throw std::invalid_argument("Null texture or empty slot");textures=std::move(value);texture_path.clear();initDone=false;invalidate();}

void Material::validateProperties(const MaterialProperties& value) {
    auto unit=[](float x){return std::isfinite(x)&&x>=0&&x<=1;};
    auto nonnegative=[](float x){return std::isfinite(x)&&x>=0;};
    if(!unit(value.alphaCutoff)||!unit(value.opacityFactor)||!unit(value.occlusionStrength)||
       !nonnegative(value.normalStrength)||(value.metallicFactor&&!unit(*value.metallicFactor))||
       (value.roughnessFactor&&!unit(*value.roughnessFactor)))throw std::invalid_argument("Invalid PBR material factors");
    for(int i=0;i<3;i++)if(!nonnegative(value.albedoFactor[i])||!nonnegative(value.emissiveFactor[i]))throw std::invalid_argument("Material colors must be finite and nonnegative");
}
