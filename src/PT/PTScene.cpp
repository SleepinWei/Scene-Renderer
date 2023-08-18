#include "PT/PTScene.h"
#include "PT/BVH.h"
#include "PT/Renderer.h"
using std::make_shared;

void PTScene::addCam(std::shared_ptr<Camera> cam)
{
    camera = cam;
}

PTScene::PTScene()
{
    camera = nullptr;
    bvh = nullptr;
    lights = make_shared<hittable_list>();
    objects = make_shared<hittable_list>();
}

void PTScene::addObject(std::shared_ptr<hittable> object)
{
    objects->add(object);
}

void PTScene::buildBVH()
{
    bvh = make_shared<BVH_Node>(objects);
}

void PTScene::addLight(std::shared_ptr<hittable> object)
{
    lights->add(object);
}

shared_ptr<hittable> PTScene::getWorld()
{
    PTConfig *config = PTConfig::GetInstance();
    if (config->bUseBVH)
    {
        return bvh;
    }
    else
    {
        return objects;
    }
}