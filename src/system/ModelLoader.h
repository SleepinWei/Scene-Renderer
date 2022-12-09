#pragma once
#include<string>
#include<memory>
#include<fstream>
#include<json/json.hpp>
#include<thread>
#include<vector>

using json = nlohmann::json;

class RenderScene;
class GameObject;

class ModelLoader {
public:
	ModelLoader();
	~ModelLoader();
	void loadSceneAsync(std::shared_ptr<RenderScene>& scene,const std::string& filename);

public:
	//std::mutex istream_lock;
	std::vector<std::thread> threadpool;

public:
	void loadObject(std::shared_ptr<RenderScene>& scene, const std::string& filename);
	void loadObjectAsync(std::shared_ptr<RenderScene>& scene,const std::string& filename);
	void loadSky(std::shared_ptr<RenderScene>& scene, const std::string& filename);
	void loadSkyAsync(std::shared_ptr<RenderScene>& scene, const std::string& filename);
	void loadTerrain(std::shared_ptr<RenderScene>& scene, const std::string& filename);
	void loadTerrainAsync(std::shared_ptr<RenderScene>& scene, const std::string& filename);
};

