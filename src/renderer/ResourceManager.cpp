#include"ResourceManager.h"
#include"Texture.h"
#include <filesystem>
#include <fstream>
#include<utility>

/*
	开始的时候先根据把GUID和对应的文件路径全都加载进来
	路径格式是从.asset/model/开始的相对路径 因为所有Unity导过来的资源都在这里
*/
ResourceManager::ResourceManager()
{
	std::string root = std::filesystem::current_path().string() + "\\asset\\model\\";
	// 只读取这三个重要文件夹
	std::vector<std::string> dirs = { "meshes","materials","textures"};
	for (std::string dir : dirs)
	{
		if (!std::filesystem::is_directory(root + dir))
		{
			std::cout << "ERROR: ResourceManager can't find directory: " + root + dir << std::endl;
			continue;
		}
		for (auto& i : std::filesystem::directory_iterator(root + dir))
		{
			std::string path = i.path().string();
			if (path.substr(path.find_last_of('.') + 1) == "meta")
			{
				// 解析meta文件中的guid
				std::ifstream meta_ifs(path, std::ios::in);
				std::string guid;
				do
				{
					meta_ifs >> guid;
				} while (guid != "guid:");
				meta_ifs >> guid;
				meta_ifs.close();

				// 添加文件guid索引
				guidMap.emplace(guid, path.substr(0, path.size() - 5));
			}
		}
	}
}

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