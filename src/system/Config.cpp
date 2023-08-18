#include "system/Config.h"
#include "PT/Connector.h"
#include<fstream>
using std::ifstream; 

void Config::parse(const string &filename)
{
    ifstream f(filename);
    json data = json::parse(f);

    if(data.find("gui")!=data.end()){
        bGui = data["gui"].get<bool>();
    }
    if(data.find("scene") != data.end()){
        scene_file = data["scene"].get<string>();
    }
    if(data.find("tracer")!=data.end()){
        Connector::GetInstance()->passDataToPTConfig(data["tracer"]);
    }
}