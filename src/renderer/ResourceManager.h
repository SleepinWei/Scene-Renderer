#pragma once

#include <optional>
#include<vector>
#include<string>
#include<unordered_map>
#include<memory>
#include<string> 

class Texture; 

class ResourceManager {
public:

	ResourceManager()=default;
	~ResourceManager();
	std::shared_ptr<Texture> find(std::string tex_name);
	std::shared_ptr<Texture> getResource(const std::string& file_path);

public:
	//const int maxlen = 10; 
	std::vector <std::shared_ptr<Texture>> resource;
	std::unordered_map<std::string, std::shared_ptr<Texture>> resourceMap; 
};

