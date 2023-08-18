#ifndef CONNECTOR_H
#define CONNECTOR_H
#include<renderer/RenderScene.h>
#include<PT/PTScene.h>
#include<memory>
#include<json/json.hpp>
#include<thread>
using nlohmann::json;
using std::shared_ptr;

// connect between path tracer and real time rendering part
class Connector{
public:
    static Connector* GetInstance(){
        static Connector connector;
        return &connector;
    }

    void buildPTSceneFromRenderScene(shared_ptr<PTScene> ptscene, const shared_ptr<RenderScene> renderScene);
    void passDataToPTConfig(json& data);
    void LaunchPathTracingWithRenderScene(shared_ptr<RenderScene> scene);

public:
    std::vector<std::thread> threadpool;
};

#endif // !CONNECTOR_H
