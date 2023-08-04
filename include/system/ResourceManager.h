#pragma once

#include <optional>
#include<vector>
#include<string>
#include<unordered_map>
#include<memory>
#include<string> 

class Texture; 

class ResourceManager {
	ResourceManager();
	~ResourceManager();
public:
	static ResourceManager *GetInstance() { 
		static ResourceManager resourceManager;
		return &resourceManager;
	}
	std::shared_ptr<Texture> find(std::string tex_name);
	std::shared_ptr<Texture> getResource(const std::string& file_path);
	std::shared_ptr<Texture> getResourceAsync(const std::string& filename);

public:
	//const int maxlen = 10;
	//std::unordered_map<std::string, std::string> guidMap;
	std::vector <std::shared_ptr<Texture>> resource;
	std::unordered_map<std::string, std::shared_ptr<Texture>> resourceMap; 
};

