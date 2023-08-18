#include "PT/Connector.h"
#include "PT/PTRenderer.h"
#include "renderer/RenderScene.h"
#include"component/GameObject.h"
#include"component/Mesh_Filter.h"
#include"component/Lights.h"
#include"PT/PTMaterial.h"
#include"system/InputManager.h"
#include"utils/Camera.h"
#include"PT/PathTracing.h"
#include"utils/log.h"
using std::make_shared;


shared_ptr<hittable_list> buildHittableFromMesh(const shared_ptr<Mesh> mesh){
    auto vertices = mesh->vertices;
    auto indices = mesh->indices;
    shared_ptr<hittable_list> result = make_shared<hittable_list>();
    shared_ptr<PTMaterial> mat = make_shared<Lambertian>(vec3{1.0f, 1.0f, 1.0f});
    int triangle_cnt = indices.size() / 3;
    for (int i = 0; i < indices.size() / 3; i++){
        if(i % 5 == 0){
            LOG_INFO("Mesh Progress " << i * 1.0f / triangle_cnt * 100 << "%");
        }
        int index0 = indices[3 * i];
        int index1 = indices[3 * i + 1];
        int index2 = indices[3 * i + 2];
        Vertex a = vertices[index0];
        Vertex b = vertices[index1];
        Vertex c = vertices[index2];

        PTVertex _a{a.Position, a.Normal, a.TexCoords};
        PTVertex _b{b.Position, b.Normal, b.TexCoords};
        PTVertex _c{c.Position, c.Normal, c.TexCoords};
        shared_ptr<hittable> tri = make_shared<Triangle>(_a,_b,_c,mat);

        result->add(tri);
    }
    return result;
}

void merge(shared_ptr<hittable_list> result , const shared_ptr<hittable_list>& h){
    for(auto o : h->objects){
        result->add(o);
    }
}

void Connector::passDataToPTConfig(json& data){
    PTConfig::GetInstance()->parse(data); 
}

void Connector::buildPTSceneFromRenderScene(shared_ptr<PTScene> ptscene, const shared_ptr<RenderScene> renderScene)
{
    //camera
    auto &camera = renderScene->main_camera;
    ptscene->addCam(make_shared<PTCamera>(
        camera->Position,
        camera->Position + camera->Front,
        camera->WorldUp,
        camera->Zoom,
        InputManager::GetInstance()->width,
        InputManager::GetInstance()->height));

    // objects
    shared_ptr<hittable_list>& pt_objects= ptscene->objects;

    int num_thread = std::min((int)renderScene->objects.size(),(int)std::thread::hardware_concurrency());
    // auto lambda = [=](int id)
    // {
        int obj_num = renderScene->objects.size();
        // int obj_per_thread = obj_num / num_thread;
        // int start =  obj_per_thread * id;
        // int end = start + obj_per_thread;
        // if(id == num_thread - 1){
        //     end = obj_num;
        // }

        for (int i = 0; i < obj_num; i++)
        {
            // if(id == 0){
                LOG_INFO("Progress: " << i * 1.0f / obj_num * 100 << "%");
            // }
            auto go = renderScene->objects[i];
            // for all game objects
            auto meshfilter = std::static_pointer_cast<MeshFilter>(go->GetComponent("MeshFilter"));
            if (!meshfilter || meshfilter->meshes.size() < 3)
            {
                continue;
            }
            auto meshes = meshfilter->meshes;
            for (auto mesh : meshes)
            {
                auto tri_s = buildHittableFromMesh(mesh);
                merge(pt_objects, tri_s);
            }
        }
    // };

    //lights
    for(auto& light : renderScene->pointLights){
        // point lights are spheres
        // todo: 
    }

    for(auto& light: renderScene->directionLights){
        // directional lights have no bounding box
        shared_ptr<DiffuseLight> mat = make_shared<DiffuseLight>(vec3(1.0,1.0,1.0));
        auto pt_object = make_shared<Directional>(light->data.direction,mat);
        ptscene->other_objects->add(pt_object);
    }

    ptscene->buildBVH();
}

void Connector::LaunchPathTracingWithRenderScene(shared_ptr<RenderScene> scene){
	shared_ptr<PTScene> pt_scene = make_shared<PTScene>();

    LOG_INFO("Scene Transformation Start");
	buildPTSceneFromRenderScene(pt_scene, scene);
    LOG_INFO("Path Tracing Begins");

    PathTracingRun(pt_scene);
}