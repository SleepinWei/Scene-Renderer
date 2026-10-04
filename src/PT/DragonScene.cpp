#include "PT/ValidationScenes.h"
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
}
