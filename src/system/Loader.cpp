#include"system/Loader.h"
#include"renderer/RenderScene.h"
#include"component/GameObject.h"
#include"object/Terrain.h"
#include"object/SkyBox.h"
#include<utility>

Loader::Loader() {

}

Loader::~Loader() {
}

void Loader::loadObjectAsync(std::shared_ptr<RenderScene> scene, json data, std::vector<std::string> objectname, int threadid) {
	//std::thread loadThread = std::thread(&Loader::loadObject, this, scene, filename);
	//threadpool.push_back(std::move(loadThread));
	//threadQueue.push(std::move(loadThread));
	int size = data.size();
	int interval = size / maxThread;
	if (size % maxThread != 0) {
		++interval;
	}
	int start = interval * threadid;
	int end = std::min(start + interval, size);

	for(int i =start ;i<end;++i){
		auto key = objectname[i];
		auto filename = data.at(key).get<std::string>();
		loadObject(scene, filename);
	}
}

void Loader::loadObject(std::shared_ptr<RenderScene>& scene, const std::string& filename) {
	std::ifstream f(filename);
	if (!f) {
		std::cout << "Loader::loadObject : Failed to load file " << filename << '\n';
		return;
	}
	json data = json::parse(f);
	std::shared_ptr<GameObject> object = std::make_shared<GameObject>();
	object->loadFromJson(data);

	scene->addObject(object);
}

void Loader::loadSceneAsync(std::shared_ptr<RenderScene>& scene, const std::string& filename) {
	//clear scene 
	// wait for all threads to end
	//for (auto& t : threadpool) {
	//	if(t.joinable())
	//		t.join();
	//}
	scene->destroy(); // destroy scene

	std::ifstream f(filename);
	if (!f) {
		std::cout << "In Loader::loadSceneAsync: Failed to open file: " << filename << '\n';
		return;
	}
	json data = json::parse(f);
	if (data.find("objects") != data.end()) {
		auto objectData = data["objects"];

		std::vector<std::string> objectname;
		for (auto iter = objectData.begin(); iter != objectData.end(); ++iter) {
			objectname.emplace_back(iter.key());
		}
		for (int i = 0; i < maxThread; ++i) {
			std::thread loadThread = std::thread(&Loader::loadObjectAsync, this, scene, objectData,objectname, i);
			threadpool.push_back(std::move(loadThread));
		}
	}
	//if (data.find("objects") != data.end()) {
	//	auto objects = data["objects"];
	//	for (auto iter = objects.begin(); iter != objects.end(); ++iter) {
	//		std::string path = iter.value().get<std::string>();
	//			// wait for one thread to end
	//		this->loadObjectAsync(scene, path);
	//	}
	//}
	if (data.find("sky") != data.end()) {
		std::string path = data["sky"].get<std::string>();
		this->loadSkyAsync(scene, path);
	}
	if (data.find("terrain") != data.end()) {
		std::string path = data["terrain"].get<std::string>();
		this->loadTerrainAsync(scene, path);
		//this->loadTerrain(scene, path);
	}

	// wait for all threads to end
	for (auto& t : threadpool) {
		t.join();
	}

}

void Loader::loadSkyAsync(std::shared_ptr<RenderScene>& scene, const std::string& filename) {
	std::thread loadThread = std::thread(&Loader::loadSky, this, scene, filename);
	threadpool.push_back(std::move(loadThread));
	//threadQueue.push(std::move(loadThread));
}

void Loader::loadSky(std::shared_ptr<RenderScene> scene, const std::string filename) {
	std::ifstream f(filename);
	if (!f) {
		std::cout << "In Loader::loadSky : Failed to open file: " << filename << '\n';
		return;
	}
	json data = json::parse(f);

	std::shared_ptr<Sky> sky = std::make_shared<Sky>();
	//scene->addObject(atm);
	sky->loadFromJson(data);
	scene->addSky(sky);
}

void Loader::loadTerrainAsync(std::shared_ptr<RenderScene>& scene, const std::string& filename) {
	std::thread loadThread = std::thread(&Loader::loadTerrain, this, scene, filename);
	threadpool.push_back(std::move(loadThread));
	//threadQueue.push(std::move(loadThread));
}

void Loader::loadTerrain(std::shared_ptr<RenderScene> scene, const std::string filename) {
	std::ifstream f(filename);
	if (!f) {
		std::cout << "In Loader::loadTerrain : Failed to open file: " << filename << '\n';
		return;
	}
	json data = json::parse(f);

	std::shared_ptr<Terrain> terrain = std::make_shared<Terrain>();
	terrain->loadFromJson(data);
	scene->addTerrain(terrain);
}
