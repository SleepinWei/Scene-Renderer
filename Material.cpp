#include"Material.h"
#include"src/Utils.h"
#include <memory>
#include<stb/stb_image.h>

void PBRMaterial::initTexture(const std::string& path) {
	albedoMap = loadTexture((path + "albedo.png").c_str());
	normalMap = loadTexture((path + "normal.png").c_str());
	metallicMap = loadTexture((path + "metallic.png").c_str());
	roughnessMap = loadTexture((path + "roughness.png").c_str());
	aoMap = loadTexture((path + "ao.png").c_str());
}

PBRMaterial::PBRMaterial(const std::string& path) {
	initTexture(path);
	materialCnt = 5; 
	beginIndex = 0;
}
void Material::setBeginIndex(int index) {
	beginIndex = index;
}
int Material::getMaterialCount()const{
	return materialCnt;
}

void PBRMaterial::render() {

	glActiveTexture(GL_TEXTURE0 + beginIndex);
	glBindTexture(GL_TEXTURE_2D, albedoMap);
	glActiveTexture(GL_TEXTURE1 + beginIndex);
	glBindTexture(GL_TEXTURE_2D, normalMap);
	glActiveTexture(GL_TEXTURE2 + beginIndex);
	glBindTexture(GL_TEXTURE_2D, metallicMap);
	glActiveTexture(GL_TEXTURE3 + beginIndex);
	glBindTexture(GL_TEXTURE_2D, roughnessMap);
	glActiveTexture(GL_TEXTURE4 + beginIndex);
	glBindTexture(GL_TEXTURE_2D, aoMap);

	shader->setInt("material.albedo", 0 + beginIndex);
	shader->setInt("material.normal", 1 + beginIndex);
	shader->setInt("material.metallic", 2 + beginIndex);
	shader->setInt("material.roughness", 3 + beginIndex);
	shader->setInt("material.ao", 4 + beginIndex);
}

void PBRMaterial::destroy() {
	glDeleteTextures(1, &albedoMap);
	glDeleteTextures(1, &normalMap);
	glDeleteTextures(1, &metallicMap);
	glDeleteTextures(1, &roughnessMap);
	glDeleteTextures(1, &aoMap);
}

PBRMaterial::~PBRMaterial() {
	destroy();
}

std::shared_ptr<Renderable> ResourceManager::registerResource(TEX_TYPE type) {
	int index = static_cast<int>(type);
	if (!resource[index]) {
		resource[index] = ResourceManager::generateResource(type);
	}
	return resource[index];
}

std::shared_ptr<Material> ResourceManager::generateResource(TEX_TYPE type) {
	switch (type)
	{
	case TEX_TYPE::RUST:
		return std::make_shared<Material>(PBRMaterial("./asset/pbr/rust/"));
		break;
	case TEX_TYPE::GRASS:
		return std::make_shared<Material>(PBRMaterial("./asset/pbr/grass/"));
		break;
	case TEX_TYPE::ROCK:
		return std::make_shared<Material>(PBRMaterial("./asset/pbr/riverrock/"));
		break;
	case TEX_TYPE::METAL:
		return std::make_shared<Material>(PBRMaterial("./asset/pbr/metal/"));
		break;
	case TEX_TYPE::SAND:
		return std::make_shared<Material>(PBRMaterial("./asset/pbr/sand/"));
		break;
	case TEX_TYPE::HEIGHT:
		return std::make_shared<Material>(HeightMaterial("./asset/heightmap/iceland/"));
	default:
		break;
	}
}

ResourceManager::ResourceManager() {
	resource = std::vector<std::shared_ptr<Renderable>>(maxlen);
}
ResourceManager::~ResourceManager() {
}


void HeightMaterial::initTexture(const std::string& path) {
	int width, height, nrChannels; 
	//xzScale = 10.0f;
	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture); // all upcoming GL_TEXTURE_2D operations now have effect on this texture object
	// set the texture wrapping parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	// set texture wrapping to GL_REPEAT (default wrapping method)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	// set texture filtering parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	unsigned char* data = stbi_load((path+"heightmap.png").c_str(), &width, &height, &nrChannels, 0);
	if (data)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
		//std::cout << "Loaded heightmap of size " << height << " x " << width << std::endl;
	}
	else
	{
		std::cout << "Failed to load texture" << std::endl;
	}
	stbi_image_free(data);
	//std::cerr << "Heightmap done" << '\n';

	glGenTextures(1, &normalTexture);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, normalTexture); // all upcoming GL_TEXTURE_2D operations now have effect on this texture object
	// set the texture wrapping parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	// set texture wrapping to GL_REPEAT (default wrapping method)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	// set texture filtering parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	int nwidth, nheight, nnrChannels;
	unsigned char *ndata = stbi_load((path + "normal.png").c_str(), &nwidth, &nheight, &nnrChannels, 0);
	if (data)
	{
		GLenum format;
		if (nnrChannels == 4) {
			format = GL_RGBA;
		}
		else if (nnrChannels == 3) {
			format = GL_RGB;
		}
		glTexImage2D(GL_TEXTURE_2D, 0, format, nwidth, nheight, 0, format, GL_UNSIGNED_BYTE, ndata);
		glGenerateMipmap(GL_TEXTURE_2D);
		//std::cout << "Loaded heightmap of size " << height << " x " << width << std::endl;
	}
	else
	{
		std::cout << "Failed to load texture" << std::endl;
	}
	stbi_image_free(ndata);
}

void HeightMaterial::registerShader(ShaderType st) {
	renderManager.registerShader(st);
}
void HeightMaterial::render() {
	glActiveTexture(GL_TEXTURE0 + beginIndex);
	glBindTexture(GL_TEXTURE_2D, texture);
	glActiveTexture(GL_TEXTURE1 + beginIndex);
	glBindTexture(GL_TEXTURE_2D, normalTexture);

	shader->setInt("heightMap", 0 + beginIndex);
	shader->setInt("normalMap", 1 + beginIndex);
}
HeightMaterial::HeightMaterial(const std::string& path) {
	initTexture(path);
	this->materialCnt = 2; 
}
HeightMaterial::~HeightMaterial() {
	if (texture) {
		glDeleteTextures(1, &texture);
	}
	if (normalTexture) {
		glDeleteTextures(1, &normalTexture);
	}
}
