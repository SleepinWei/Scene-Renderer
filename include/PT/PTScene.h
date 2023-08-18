#ifndef PT_SCENE_H
#define PT_SCENE_H
#include"PT/hittable.h"
#include"PT/PTCamera.h"
#include"PT/BVH.h"
#include<memory>

using std::shared_ptr;
using std::make_shared; 
using namespace PT;

class RenderScene;
class PTScene{
public:
    PTScene();
    shared_ptr<PTCamera> camera;
    shared_ptr<BVH_Node> bvh;
    shared_ptr<hittable_list> lights; 
    shared_ptr<hittable_list> objects; 

public:
    void addObject(shared_ptr<hittable> object);
    void addLight(shared_ptr<hittable> object);
    void addCam(shared_ptr<PTCamera> cam);
    void buildBVH();
    // void buildFromRenderScene(shared_ptr<RenderScene> renderScene);
    shared_ptr<hittable> getWorld();
};

#endif // !PT_SCENE_H