#include"system/Loader.h"
#include"renderer/RenderScene.h"
#include"component/GameObject.h"
#include"object/Terrain.h"
#include"object/SkyBox.h"
#include<utility>

Loader::Loader() {
	const unsigned cores=std::thread::hardware_concurrency();maxThread=cores>2?int(std::min(cores-2,32u)):1;
}

Loader::~Loader() {for(auto& thread:threadpool)if(thread.joinable())thread.join();}
void Loader::launch(std::function<void()> task){
    threadpool.emplace_back([this,task=std::move(task)]{try{task();}catch(...){std::lock_guard<std::mutex> lock(errorMutex_);if(!workerError_)workerError_=std::current_exception();}});
}
void Loader::finish(){
    for(auto& thread:threadpool)if(thread.joinable())thread.join();threadpool.clear();
    std::exception_ptr error;{std::lock_guard<std::mutex> lock(errorMutex_);error=std::exchange(workerError_,{});}if(error)std::rethrow_exception(error);
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

void Loader::loadSceneAsync(std::shared_ptr<RenderScene>& scene,const std::string& filename){
    finish();if(!scene || maxThread<1)throw std::invalid_argument("Loader needs a scene and positive worker count");
    std::ifstream input(filename);if(!input)throw std::invalid_argument("Cannot open scene: "+filename);json data=json::parse(input);
    // Parse before clearing the current scene; malformed JSON leaves it intact.
    scene->destroy();
    try{
        if(data.contains("objects")){
            auto objectData=data.at("objects");std::vector<std::string> names;for(auto it=objectData.begin();it!=objectData.end();++it)names.push_back(it.key());
            for(int i=0;i<std::min(maxThread,int(objectData.size()));i++)launch([this,scene,objectData,names,i]{loadObjectAsync(scene,objectData,names,i);});
        }
        if(data.contains("sky"))loadSkyAsync(scene,data.at("sky").get<std::string>());
        if(data.contains("terrain"))loadTerrainAsync(scene,data.at("terrain").get<std::string>());
        finish();
    }catch(...){auto error=std::current_exception();try{finish();}catch(...){}std::rethrow_exception(error);}
}

void Loader::loadSkyAsync(std::shared_ptr<RenderScene>& scene, const std::string& filename) {
    launch([this,scene,filename]{loadSky(scene,filename);});
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
    launch([this,scene,filename]{loadTerrain(scene,filename);});
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
