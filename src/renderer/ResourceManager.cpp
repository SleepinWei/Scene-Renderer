#include"renderer/ResourceManager.h"
#include"renderer/Texture.h"
#include <filesystem>
#include <fstream>
#include<utility>

/*
	��ʼ��ʱ���ȸ��ݰ�GUID�Ͷ�Ӧ���ļ�·��ȫ�����ؽ���
	·����ʽ�Ǵ�.asset/model/��ʼ�����·�� ��Ϊ����Unity����������Դ��������
*/
ResourceManager::ResourceManager()
{
	//std::string root = std::filesystem::current_path().string() + "\\asset\\model\\";
	//// ֻ��ȡ��������Ҫ�ļ���
	//// clear
	//std::unordered_map<std::string, std::string>().swap(guidMap); 

	//std::vector<std::string> dirs = { "meshes","materials","textures"};
	//for (std::string dir : dirs)
	//{
	//	if (!std::filesystem::is_directory(root + dir))
	//	{
	//		std::cout << "ERROR: ResourceManager can't find directory: " + root + dir << std::endl;
	//		continue;
	//	}
	//	for (auto& i : std::filesystem::directory_iterator(root + dir))
	//	{
	//		std::string path = i.path().string();
	//		if (path.substr(path.find_last_of('.') + 1) == "meta")
	//		{
	//			// ����meta�ļ��е�guid
	//			std::ifstream meta_ifs(path, std::ios::in);
	//			std::string guid;
	//			do
	//			{
	//				meta_ifs >> guid;
	//			} while (guid != "guid:");
	//			meta_ifs >> guid;
	//			meta_ifs.close();

	//			// ����ļ�guid����
	//			guidMap.emplace(guid, path.substr(0, path.size() - 5));
	//		}
	//	}
	//}
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

std::shared_ptr<Texture> ResourceManager::getResourceAsync(const std::string& filename) {
	auto iter = resourceMap.find(filename);
	if (iter == resourceMap.end()) {
		// can't find
		auto tex = Texture::loadFromFileAsync(filename);
		//resource.push_back(tex);
		resourceMap.insert(std::make_pair(tex->name,tex));
		return tex;
	}
	return iter->second;
}