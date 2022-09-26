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
	//std::shared_ptr<Texture> tex = Texture::loadFromFile(file_path);
	auto iter = resourceMap.find(file_path);
	if (iter == resourceMap.end()) {
		// can't find
		auto tex = Texture::loadFromFile(file_path);
		resource.push_back(tex);
		resourceMap.insert(std::make_pair(tex->name,tex));
		return tex;
	}
	return iter->second;
}