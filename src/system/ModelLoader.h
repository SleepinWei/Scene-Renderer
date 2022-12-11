#pragma once
#include<string>
#include<memory>
#include<fstream>
#include<json/json.hpp>
#include<thread>
//#include<vector>
#include<queue>

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
	//std::queue<std::thread> threadQueue;
	const int maxThread = 16;

public:
	void loadObject(std::shared_ptr<RenderScene>& scene, const std::string& filename);
	void loadObjectAsync(std::shared_ptr<RenderScene>& scene,json& data,std::vector<std::string>& objectname,int threadid);
	void loadSky(std::shared_ptr<RenderScene>& scene, const std::string& filename);
	void loadSkyAsync(std::shared_ptr<RenderScene>& scene, const std::string& filename);
	void loadTerrain(std::shared_ptr<RenderScene>& scene, const std::string& filename);
	void loadTerrainAsync(std::shared_ptr<RenderScene>& scene, const std::string& filename);
};

