#include"ResourceManager.h"
#include"Texture.h"
#include<utility>

std::shared_ptr<Texture> ResourceManager::find(std::string tex_name) {
	auto iter = resourceMap.find(tex_name);
	if (iter == resourceMap.end()) {
		return nullptr; 
	}
	return resourceMap[tex_name];
}

ResourceManager::~ResourceManager() {

}

std::shared_ptr<Texture> ResourceManager::getResource(const std::string& file_path) {
	std::shared_ptr<Texture> tex = getResource(file_path);
	if (!tex) {
		tex = Texture::loadFromFile(file_path);
		resource.push_back(tex);
		resourceMap.insert(std::make_pair(tex->type,tex));
	}
	return tex; 
}