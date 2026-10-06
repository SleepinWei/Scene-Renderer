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
ValidationScene makePoolCausticsScene(uint32_t width,uint32_t height,bool waves,bool water,bool sunlit,bool underwater){
    ValidationScene result;auto &s=result.snapshot;s.exposure=1;
    s.frame.cameraPosition={4.8f,5.6f,6.6f};glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    s.frame.viewProjection=depth*glm::perspective(glm::radians(40.f),float(width)/height,.1f,100.f)*glm::lookAt(s.frame.cameraPosition,glm::vec3(0,.5f,0),glm::vec3(0,1,0));
    if(sunlit){s.frame.cameraPosition=underwater?glm::vec3(0,.85f,1.4f):glm::vec3(3.8f,6.4f,5.2f);s.frame.viewProjection=depth*glm::perspective(glm::radians(underwater?48.f:40.f),float(width)/height,.05f,100.f)*glm::lookAt(s.frame.cameraPosition,underwater?glm::vec3(0,0,.4f):glm::vec3(0,.25f,0),glm::vec3(0,1,0));}
    // Shared tile payload keeps the fixture small while exposing refractive distortion.
    auto tile=quad({0,0,0},{0,0,1},{1,0,1},{1,0,0});
    const int tiles=sunlit?25:10;const float pitch=5.f/tiles;
    for(int x=0;x<tiles;++x)for(int z=0;z<tiles;++z){auto draw=tile;draw.model=glm::translate(glm::mat4(1),glm::vec3(-2.5f+x*pitch,0,-2.5f+z*pitch))*glm::scale(glm::mat4(1),glm::vec3(pitch-(sunlit?.003f:0),1,pitch-(sunlit?.003f:0)));draw.objectId=1+x*tiles+z;draw.parameters.albedoAlpha=glm::vec4(sunlit?glm::vec3(.58f,.8f,.85f)+float((x*7+z*11)%3-1)*.006f:(x+z)%2?glm::vec3(.62f,.72f,.76f):glm::vec3(.85f,.88f,.86f),1);s.draws.push_back(draw);}
    for(auto wall:{quad({-2.5f,0,-2.5f},{2.5f,0,-2.5f},{2.5f,1.6f,-2.5f},{-2.5f,1.6f,-2.5f}),quad({-2.5f,0,2.5f},{-2.5f,0,-2.5f},{-2.5f,1.6f,-2.5f},{-2.5f,1.6f,2.5f})}){wall.parameters.albedoAlpha={.68f,.76f,.8f,1};s.draws.push_back(wall);}
    if(sunlit){
        auto grout=quad({-2.5f,-.001f,-2.5f},{-2.5f,-.001f,2.5f},{2.5f,-.001f,2.5f},{2.5f,-.001f,-2.5f});grout.parameters.albedoAlpha={.45f,.57f,.6f,1};s.draws.push_back(grout);
        for(auto wall:{quad({2.5f,0,-2.5f},{2.5f,0,2.5f},{2.5f,1.6f,2.5f},{2.5f,1.6f,-2.5f}),quad({2.5f,0,2.5f},{-2.5f,0,2.5f},{-2.5f,1.6f,2.5f},{2.5f,1.6f,2.5f})}){wall.parameters.albedoAlpha={.68f,.76f,.8f,1};s.draws.push_back(wall);}
        // A concrete deck surrounds the pool, so the above-water view has context.
        for(auto slab:{quad({-4,1.6f,-4},{-4,1.6f,-2.5f},{4,1.6f,-2.5f},{4,1.6f,-4}),quad({-4,1.6f,2.5f},{-4,1.6f,4},{4,1.6f,4},{4,1.6f,2.5f}),quad({-4,1.6f,-2.5f},{-4,1.6f,2.5f},{-2.5f,1.6f,2.5f},{-2.5f,1.6f,-2.5f}),quad({2.5f,1.6f,-2.5f},{2.5f,1.6f,2.5f},{4,1.6f,2.5f},{4,1.6f,-2.5f})}){slab.parameters.albedoAlpha={.83f,.81f,.75f,1};s.draws.push_back(slab);}
    }
    if(water){
        auto mesh=std::make_shared<render::MeshPayload>();const uint32_t grid=sunlit?192:96;
        for(uint32_t z=0;z<=grid;++z)for(uint32_t x=0;x<=grid;++x){float X=-2.49f+4.98f*x/grid,Z=-2.49f+4.98f*z/grid;
            float a=3.2f*X+1.4f*Z,b=-1.8f*X+4.1f*Z,h=waves?.085f*std::sin(a)+.045f*std::sin(b):0;
            float dx=waves?.085f*3.2f*std::cos(a)-.045f*1.8f*std::cos(b):0,dz=waves?.085f*1.4f*std::cos(a)+.045f*4.1f*std::cos(b):0;
            if(sunlit){h=dx=dz=0;if(waves)for(auto wave:{glm::vec4(.026f,9.1f,3.7f,.2f),glm::vec4(.020f,-4.7f,12.3f,1.7f),glm::vec4(.013f,16.5f,-8.5f,2.3f),glm::vec4(.008f,5.8f,23.1f,.8f),glm::vec4(.005f,-21,14,3.4f)}){float phase=wave.y*X+wave.z*Z+wave.w;h+=wave.x*std::sin(phase);dx+=wave.x*wave.y*std::cos(phase);dz+=wave.x*wave.z*std::cos(phase);}}
            mesh->vertices.push_back({{X,1.15f+h,Z},glm::normalize(glm::vec3(-dx,1,-dz)),{float(x)/grid,float(z)/grid}});
        }
        for(uint32_t z=0;z<grid;++z)for(uint32_t x=0;x<grid;++x){uint32_t a=z*(grid+1)+x,b=a+grid+1;mesh->indices.insert(mesh->indices.end(),{a,b,a+1,a+1,b,b+1});}
        auto face=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d){auto q=quad(a,b,c,d).mesh;uint32_t base=uint32_t(mesh->vertices.size());mesh->vertices.insert(mesh->vertices.end(),q->vertices.begin(),q->vertices.end());for(auto i:q->indices)mesh->indices.push_back(base+i);};
        // Match each side strip to the sampled wave edge, forming a closed volume.
        // Separate water and wall planes by 1 cm to avoid ambiguous coplanar hits.
        for(uint32_t i=0;i<grid;++i){auto v=[&](uint32_t x,uint32_t z){return mesh->vertices[z*(grid+1)+x].position;};auto bottom=[](glm::vec3 p){p.y=-.2f;return p;};
            auto a=v(i,0),b=v(i+1,0);face(bottom(b),bottom(a),a,b);
            a=v(i,grid);b=v(i+1,grid);face(bottom(a),bottom(b),b,a);
            a=v(0,i);b=v(0,i+1);face(bottom(a),bottom(b),b,a);
            a=v(grid,i);b=v(grid,i+1);face(bottom(b),bottom(a),a,b);
        }
        face({-2.49f,-.2f,-2.49f},{2.49f,-.2f,-2.49f},{2.49f,-.2f,2.49f},{-2.49f,-.2f,2.49f});
        render::SnapshotDraw draw;draw.mesh=mesh;draw.objectId=1000;draw.parameters.albedoAlpha=glm::vec4(1);draw.pathTracingKind=3;draw.pathTracingIor=1.333f;draw.pathTracingAbsorption=sunlit?glm::vec3(.065f,.015f,.007f):glm::vec3(.08f,.025f,.012f);draw.pathTracingScattering=glm::vec3(0);draw.pathTracingRoughness=0;s.draws.push_back(draw);
    }return result;
}
ValidationScene makeMediumValidationScene(uint32_t width,uint32_t height,bool conservative){
    ValidationScene result;auto &s=result.snapshot;s.frame.cameraPosition={0,.2f,4};glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;s.frame.viewProjection=depth*glm::perspective(glm::radians(35.f),float(width)/height,.1f,100.f)*glm::lookAt(s.frame.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));
    auto box=[&](float r,uint32_t kind,float ior,glm::vec3 absorption,glm::vec3 scattering,float g){
        auto mesh=std::make_shared<render::MeshPayload>();
        auto face=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d){auto q=quad(a*r,b*r,c*r,d*r).mesh;uint32_t base=uint32_t(mesh->vertices.size());mesh->vertices.insert(mesh->vertices.end(),q->vertices.begin(),q->vertices.end());for(auto i:q->indices)mesh->indices.push_back(base+i);};
        face({-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1});face({-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1});face({-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1});face({1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1});face({-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1});face({1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1});
        render::SnapshotDraw draw;draw.mesh=mesh;draw.objectId=kind;draw.parameters.albedoAlpha=glm::vec4(1);draw.parameters.factors={0,.1f,1,0};draw.pathTracingKind=kind;draw.pathTracingIor=ior;draw.pathTracingAbsorption=absorption;draw.pathTracingScattering=scattering;draw.pathTracingAnisotropy=g;s.draws.push_back(draw);
    };
    if(!conservative)box(1.8f,3,1.333f,{.04f,.02f,.01f},{.1f,.2f,.3f},.65f);
    box(1,4,1.54f,conservative?glm::vec3(0):glm::vec3(2,.1f,.8f),conservative?glm::vec3(.6f,.8f,.7f):glm::vec3(6,8,7),.3f);return result;
}
ValidationScene makeWaterSolarValidationScene(uint32_t width,uint32_t height){
    auto result=makeMediumValidationScene(width,height,true);auto &s=result.snapshot;
    auto &water=s.draws[0];water.pathTracingKind=3;water.pathTracingIor=1.333f;water.pathTracingAbsorption=glm::vec3(1000);water.pathTracingScattering=glm::vec3(0);water.pathTracingAnisotropy=0;
    water.model=glm::translate(glm::mat4(1),glm::vec3(0,-10,0))*glm::scale(glm::mat4(1),glm::vec3(1000,10,1000));
    auto wall=quad({-20,.1f,0},{20,.1f,0},{20,1.9f,0},{-20,1.9f,0});wall.pathTracingBsdfModel=1;wall.parameters.albedoAlpha=glm::vec4(std::pow(.8f,1/2.2f),std::pow(.8f,1/2.2f),std::pow(.8f,1/2.2f),1);s.draws.push_back(wall);
    // Block direct sun at the wall; the reflected chain exits beyond this roof.
    s.draws.push_back(quad({-100,2,-100},{100,2,-100},{100,2,10},{-100,2,10}));
    s.frame.cameraPosition={0,1,2};glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    s.frame.viewProjection=depth*glm::perspective(glm::radians(35.f),float(width)/height,.1f,100.f)*glm::lookAt(s.frame.cameraPosition,glm::vec3(0,1,0),glm::vec3(0,1,0));
    return result;
}
ValidationScene makeThinSolarValidationScene(uint32_t width,uint32_t height,bool overlappingHint){
    auto result=makeWaterSolarValidationScene(width,height);auto &s=result.snapshot;
    auto mirror=quad({-1000,0,-1000},{-1000,0,1000},{1000,0,1000},{1000,0,-1000});
    mirror.pathTracingKind=5;mirror.pathTracingIor=1.5f;mirror.parameters.albedoAlpha={0,0,0,1};s.draws[0]=mirror;
    // After reflection, the ray crosses two tinted parallel sheets before the sun.
    for(float z:{20.f,21.f}){auto pane=quad({-100,-100,z},{100,-100,z},{100,100,z},{-100,100,z});pane.pathTracingKind=5;pane.pathTracingIor=1.5f;pane.parameters.albedoAlpha={.8f,.9f,1,1};s.draws.push_back(pane);}
    if(overlappingHint){auto unused=mirror;unused.model=glm::translate(glm::mat4(1),glm::vec3(10000,0,0))*glm::rotate(glm::mat4(1),.002f,glm::vec3(1,0,0))*glm::scale(glm::mat4(1),glm::vec3(-.1f,2,.2f));s.draws.push_back(unused);}
    return result;
}
}
