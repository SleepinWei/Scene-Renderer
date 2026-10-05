#include "PT/ValidationScenes.h"
#include "PT/SubsurfaceMesh.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
namespace pt {
ValidationScene makeDragonScene(uint32_t width,uint32_t height,const std::string &path,bool glass){
    Assimp::Importer importer;const auto *asset=importer.ReadFile(path,aiProcess_Triangulate|aiProcess_JoinIdenticalVertices|aiProcess_GenSmoothNormals|aiProcess_PreTransformVertices);
    if(!asset||!asset->HasMeshes())throw std::runtime_error("PT: cannot load Stanford Dragon "+path+"; run python3 tools/fetch_dragon.py");
    auto mesh=std::make_shared<render::MeshPayload>();glm::vec3 low(1e30f),high(-1e30f);
    for(uint32_t j=0;j<asset->mNumMeshes;++j){const auto &source=*asset->mMeshes[j];if(!source.HasNormals())throw std::runtime_error("PT: Dragon mesh has no surface normals");const uint32_t base=uint32_t(mesh->vertices.size());for(uint32_t i=0;i<source.mNumVertices;++i){auto p=source.mVertices[i],n=source.mNormals[i];glm::vec3 v{p.x,p.y,p.z};low=glm::min(low,v);high=glm::max(high,v);mesh->vertices.push_back({v,{n.x,n.y,n.z},{0,0}});}for(uint32_t i=0;i<source.mNumFaces;++i){const auto &face=source.mFaces[i];if(face.mNumIndices!=3)continue;for(uint32_t k=0;k<3;++k)mesh->indices.push_back(base+face.mIndices[k]);}}
    const float scale=2.f/(high.y-low.y);if(!std::isfinite(scale)||mesh->indices.empty())throw std::runtime_error("PT: invalid Stanford Dragon bounds");
    for(auto &vertex:mesh->vertices)vertex.position=(vertex.position-glm::vec3((low.x+high.x)*.5f,low.y,(low.z+high.z)*.5f))*scale+glm::vec3(0,.03f,0);
    auto result=makeCausticsScene(width,height,false);auto &s=result.snapshot;s.frame.cameraPosition={3.9f,2.8f,5.8f};glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    s.frame.viewProjection=depth*glm::perspective(glm::radians(40.f),float(width)/height,.1f,100.f)*glm::lookAt(s.frame.cameraPosition,glm::vec3(0,1,0),glm::vec3(0,1,0));
    auto studioFloor=std::make_shared<render::MeshPayload>(*s.draws[0].mesh);for(auto &vertex:studioFloor->vertices)if(vertex.position.z>0)vertex.position.z=12;s.draws[0].mesh=studioFloor;
    auto studioBack=std::make_shared<render::MeshPayload>(*s.draws[1].mesh);for(auto &vertex:studioBack->vertices)if(vertex.position.y>0)vertex.position.y=8;s.draws[1].mesh=studioBack;
    s.draws[2].parameters.emissiveNormal*=4.f;
    // A broad studio fill makes the refracted silhouette visible, while the small
    // overhead emitter retains the focused caustic on the diffuse floor.
    auto fill=std::make_shared<render::MeshPayload>();fill->vertices={{{-3,1.5f,8},{0,0,-1},{0,0}},{{3,1.5f,8},{0,0,-1},{1,0}},{{3,4.5f,8},{0,0,-1},{1,1}},{{-3,4.5f,8},{0,0,-1},{0,1}}};fill->indices={0,2,1,0,3,2};render::SnapshotDraw light;light.objectId=5;light.mesh=fill;light.parameters.albedoAlpha={0,0,0,1};light.parameters.emissiveNormal={3.8f,4.6f,5.6f,0};s.draws.push_back(light);
    auto side=std::make_shared<render::MeshPayload>();side->vertices={{{-4,0,12},{1,0,0},{0,0}},{{-4,0,-4},{1,0,0},{1,0}},{{-4,8,-4},{1,0,0},{1,1}},{{-4,8,12},{1,0,0},{0,1}}};side->indices={0,1,2,0,2,3};render::SnapshotDraw wall;wall.objectId=6;wall.mesh=side;wall.parameters.albedoAlpha={.6f,.6f,.6f,1};wall.parameters.factors={0,.8f,1,0};s.draws.push_back(wall);
    render::SnapshotDraw dragon;dragon.objectId=4;dragon.mesh=mesh;dragon.parameters.albedoAlpha=glass?glm::vec4(.99f,.995f,1,1):glm::vec4(.55f,.65f,.8f,1);dragon.parameters.factors={0,.08f,1,0};s.draws.push_back(dragon);if(glass)result.dielectrics.push_back({4,1.5f});
    std::cout<<"Stanford Dragon: "<<mesh->vertices.size()<<" vertices, "<<mesh->indices.size()/3<<" triangles, IOR "<<(glass?1.5:0)<<'\n';return result;
}
ValidationScene makeJadeDragonScene(uint32_t width,uint32_t height,const std::string &path,bool ocean,float floorDepth){
    if(!std::isfinite(floorDepth)||floorDepth<=0)throw std::invalid_argument("PT: ocean floor depth must be positive");
    auto result=makeDragonScene(width,height,path,false);auto &s=result.snapshot;auto &dragon=s.draws.back();auto mesh=std::make_shared<render::MeshPayload>(*dragon.mesh);auto closure=closeSubsurfaceMesh(*mesh);dragon.mesh=mesh;
    dragon.parameters.albedoAlpha=glm::vec4(1);dragon.pathTracingIor=1.54f;dragon.pathTracingRoughness=.22f;dragon.pathTracingAbsorption={9,.7f,3.5f};dragon.pathTracingScattering={35,45,38};dragon.pathTracingAnisotropy=.45f;dragon.pathTracingKind=4;
    std::cout<<"Jade scan closure: "<<closure.weldedVertices<<" welded vertices, "<<closure.removedFaces<<" removed faces, "<<closure.filledHoles<<" filled holes, "<<closure.boundaryEdges<<" boundary edges, "<<mesh->indices.size()/3<<" solid triangles\n";
    if(ocean){
        auto jade=dragon;s.draws.clear();s.draws.push_back(jade);
        const float bottomY=.2f-floorDepth;
        auto bottom=std::make_shared<render::MeshPayload>();bottom->vertices={{{-300,bottomY,-300},{0,1,0},{0,0}},{{-300,bottomY,300},{0,1,0},{0,1}},{{300,bottomY,300},{0,1,0},{1,1}},{{300,bottomY,-300},{0,1,0},{1,0}}};bottom->indices={0,1,2,0,2,3};render::SnapshotDraw seabed;seabed.objectId=7;seabed.mesh=bottom;seabed.parameters.albedoAlpha={.72f,.68f,.55f,1};seabed.parameters.factors={0,.85f,1,0};s.draws.push_back(seabed);
        render::OceanSurfaceSettings water;water.spectrum.size=256;water.spectrum.length=64;water.spectrum.amplitude=.0001f;water.spectrum.heightScale=.25f;water.spectrum.windSpeed=8;water.surfaceLength=512;water.meshSize=1025;water.seaLevel=.2f;water.detailStrength=.4f;water.absorption={.12f,.04f,.02f};water.scattering={.025f,.05f,.07f};s.frame.oceans={water};s.frame.sky=true;s.frame.lights={{{0,0,0,0},{30,27,24,0},{-.45f,-.7f,-.3f,0}}};s.exposure=1;
    }else{
        // Large luminous panels illuminate the thin horns and folds through the solid.
        for(auto &draw:s.draws)if(draw.objectId==2){draw.parameters.albedoAlpha={.08f,.09f,.09f,1};draw.parameters.emissiveNormal=glm::vec4(0);}
    }
    return result;
}

}
