#ifndef CONFIG_H
#define CONFIG_H

#include<json/json.hpp>
#include<string>

using nlohmann::json;
using std::string; 

class Config
{
public:
    static Config *GetInstance()
    {
        static Config config;
        return &config;
    }

    void parse(const string &filename);

public:
    bool bGui;
    string scene_file;
};

#endif // !CONFIG_H
