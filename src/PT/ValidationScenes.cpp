#include "PT/ValidationScenes.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
namespace pt {
namespace {
render::SnapshotDraw quad(glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d){
    render::SnapshotDraw draw;auto mesh=std::make_shared<render::MeshPayload>();auto n=glm::normalize(glm::cross(b-a,c-a));mesh->vertices={{a,n,{0,0}},{b,n,{1,0}},{c,n,{1,1}},{d,n,{0,1}}};mesh->indices={0,1,2,0,2,3};draw.mesh=mesh;draw.parameters.factors={0,.8f,1,0};return draw;
}
render::SnapshotDraw sphere(glm::vec3 center,float radius,uint64_t id){
    auto mesh=std::make_shared<render::MeshPayload>();constexpr uint32_t rows=32,columns=64;constexpr float pi=3.14159265358979323846f;
    for(uint32_t y=0;y<=rows;++y)for(uint32_t x=0;x<=columns;++x){float theta=pi*y/rows,phi=2*pi*x/columns;glm::vec3 n{std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)};mesh->vertices.push_back({center+radius*n,n,{float(x)/columns,float(y)/rows}});}
    for(uint32_t y=0;y<rows;++y)for(uint32_t x=0;x<columns;++x){uint32_t a=y*(columns+1)+x,b=a+columns+1;mesh->indices.insert(mesh->indices.end(),{a,b,a+1,a+1,b,b+1});}
    render::SnapshotDraw draw;draw.objectId=id;draw.mesh=mesh;draw.parameters.albedoAlpha={.99f,.995f,1,1};draw.parameters.factors={0,.05f,1,0};return draw;
}
}
ValidationScene makeCausticsScene(uint32_t width,uint32_t height,bool glass){
    ValidationScene result;auto &s=result.snapshot;s.frame.cameraPosition={4.2f,3.4f,6};glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    s.frame.viewProjection=depth*glm::perspective(glm::radians(42.f),float(width)/height,.1f,100.f)*glm::lookAt(s.frame.cameraPosition,glm::vec3(0,.7f,0),glm::vec3(0,1,0));s.exposure=2;
    auto floor=quad({-4,0,-4},{-4,0,4},{4,0,4},{4,0,-4});floor.objectId=1;floor.parameters.albedoAlpha={.65f,.65f,.65f,1};s.draws.push_back(floor);
    auto wall=quad({-4,0,-4},{4,0,-4},{4,4,-4},{-4,4,-4});wall.objectId=2;wall.parameters.albedoAlpha={.55f,.58f,.65f,1};s.draws.push_back(wall);
    auto source=quad({-.75f,5,-.45f},{-.35f,5,-.45f},{-.35f,5,-.05f},{-.75f,5,-.05f});source.objectId=3;source.parameters.emissiveNormal={90,82,68,0};source.parameters.albedoAlpha={0,0,0,1};s.draws.push_back(source);
    if(glass){s.draws.push_back(sphere({0,1.05f,0},.7f,4));result.dielectrics.push_back({4,1.5f});}
    return result;
}
}
