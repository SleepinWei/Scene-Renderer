#include "renderer/rhi/VegetationGeometry.h"
#include "renderer/rhi/TreeGeometry.h"
#include "renderer/rhi/SceneSnapshot.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_precision.hpp>
#include <algorithm>
namespace render {
namespace {
struct Builder {
    std::shared_ptr<MeshPayload> mesh=std::make_shared<MeshPayload>();
    void tri(glm::vec3 a,glm::vec3 b,glm::vec3 c,int colour){
        auto n=glm::cross(b-a,c-a);if(glm::dot(n,n)<1e-12f)return;n=glm::normalize(n);
        const auto first=uint32_t(mesh->vertices.size());
        for(auto p:{a,b,c})mesh->vertices.push_back({p,n,{(colour+.5f)/4.f,.5f}});
        for(uint32_t i=0;i<3;i++)mesh->indices.push_back(first+i);
    }
    void tube(glm::vec3 a,glm::vec3 b,float ra,float rb,int sides,int colour){
        auto axis=glm::normalize(b-a),u=glm::normalize(glm::cross(axis,std::abs(axis.y)<.9f?glm::vec3(0,1,0):glm::vec3(1,0,0))),v=glm::cross(axis,u);
        for(int i=0;i<sides;i++){float t=i*glm::two_pi<float>()/sides,q=(i+1)*glm::two_pi<float>()/sides;auto p=u*std::cos(t)+v*std::sin(t),r=u*std::cos(q)+v*std::sin(q);tri(a+ra*p,b+rb*p,a+ra*r,colour);tri(a+ra*r,b+rb*p,b+rb*r,colour);}
    }
    void leaf(glm::vec3 centre,glm::vec3 axis,float width,int colour){
        auto side=glm::normalize(glm::cross(axis,glm::vec3(.2f,1,.3f)))*width;
        tri(centre-axis*.5f,centre-side,centre+axis*.5f,colour);tri(centre-axis*.5f,centre+axis*.5f,centre+side,colour);
    }
};
}
std::shared_ptr<const MeshPayload> plantGeometry(PlantType type,uint32_t lod){
    if(uint32_t(type)>7||lod>1)throw std::invalid_argument("Unknown plant geometry");
    if(type==PlantType::Broadleaf||type==PlantType::Conifer||type==PlantType::Shrub){
        auto tree=treeGeometry(type,lod,type==PlantType::Broadleaf?71:type==PlantType::Conifer?89:97);auto mesh=std::make_shared<MeshPayload>(*tree.bark);uint32_t offset=uint32_t(mesh->vertices.size());mesh->vertices.insert(mesh->vertices.end(),tree.foliage->vertices.begin(),tree.foliage->vertices.end());for(auto index:tree.foliage->indices)mesh->indices.push_back(offset+index);return mesh;
    }
    Builder b;const bool coarse=lod!=0;
    if(type==PlantType::Broadleaf||type==PlantType::Shrub){
        bool shrub=type==PlantType::Shrub;
        b.tube({0,0,0},{0,shrub?.45f:.7f,0},shrub?.025f:.035f,.009f,coarse?4:7,0);
        int branches=coarse?4:9;
        for(int j=0;j<branches;j++){
            float a=j*2.3999632f,y=.42f+.25f*float(j%3)/2.f;
            glm::vec3 end(std::cos(a)*.25f,y+.14f,std::sin(a)*.25f);
            b.tube({0,y-.15f,0},end,.012f,.004f,coarse?3:5,0);
            int leaves=coarse?5:35;
            for(int i=0;i<leaves;i++){float p=i*2.3999632f+j,s=std::sqrt((i+.5f)/leaves);glm::vec3 offset(std::cos(p)*s*.20f,.13f*std::sin(i*1.7f)+.08f,std::sin(p)*s*.20f);
                b.leaf(end+offset,{.07f*std::cos(p),coarse?.18f:.09f,.07f*std::sin(p)},coarse?.09f:.045f,1+i%2);}
        }
    }else if(type==PlantType::Conifer){
        b.tube({0,0,0},{0,1,0},.03f,.004f,coarse?4:7,0);
        int tiers=coarse?4:8,sides=coarse?6:14;
        for(int j=0;j<tiers;j++){float y=.2f+.7f*j/tiers,r=.38f*(1-y);
            for(int i=0;i<sides;i++){float a=glm::two_pi<float>()*i/sides,q=glm::two_pi<float>()*(i+1)/sides;
                glm::vec3 p(std::cos(a)*r,y,std::sin(a)*r),n(std::cos(q)*r,y,std::sin(q)*r);
                if(coarse)b.tri(p,{0,std::min(1.f,y+.25f),0},n,1+(j%2));
                else {
                    auto end=glm::vec3(std::cos(a)*r,y-.035f,std::sin(a)*r);b.tube({0,y+.065f,0},end,.007f,.002f,3,0);
                    for(int k=0;k<7;k++){float t=(k+.5f)/7;auto centre=glm::mix(glm::vec3(0,y+.065f,0),end,t);
                        glm::vec3 radial(std::cos(a),.4f,std::sin(a)),side(-std::sin(a),.3f,std::cos(a));
                        b.leaf(centre+side*.04f,radial*.10f+side*.12f,.022f,1+(j%2));b.leaf(centre-side*.04f,radial*.10f-side*.12f,.022f,2-(j%2));}
                }}}
    }else if(type==PlantType::Daisy||type==PlantType::Wildflower){
        bool daisy=type==PlantType::Daisy;int stems=coarse?1:(daisy?3:4);
        for(int j=0;j<stems;j++){
            float angle=j*2.3999632f;glm::vec3 top(.10f*std::cos(angle),.65f+.22f*(j%2),.10f*std::sin(angle));
            b.tube({0,0,0},top,.008f,.004f,coarse?3:5,1);
            for(int k=0;k<(coarse?2:5);k++)b.leaf(top*(.15f+.12f*k),{.20f*std::cos(angle+k),.1f,.20f*std::sin(angle+k)},.035f,2);
            int petals=coarse?5:(daisy?12:7);
            for(int i=0;i<petals;i++){float a=i*glm::two_pi<float>()/petals;auto axis=glm::vec3(std::cos(a),.15f,std::sin(a))*(daisy?.15f:.11f);b.leaf(top+axis*.48f,axis,coarse?.035f:.025f,3);}
            for(int i=0;i<8;i++){float a=i*glm::two_pi<float>()/8,q=(i+1)*glm::two_pi<float>()/8;b.tri(top+glm::vec3(0,.015f,0),top+glm::vec3(.025f*std::cos(a),0,.025f*std::sin(a)),top+glm::vec3(.025f*std::cos(q),0,.025f*std::sin(q)),0);}
        }
    }else{
        int blades=coarse?6:(type==PlantType::Reed?8:12),segments=coarse?2:4;
        for(int j=0;j<blades;j++){float a=j*2.3999632f,base=.10f*(j%3),h=.7f+.3f*std::sin(j+1.f)*std::sin(j+1.f),width=type==PlantType::Reed?.018f:(coarse?.052f:.032f);
            glm::vec3 side(std::cos(a)*width,0,std::sin(a)*width);
            for(int k=0;k<segments;k++){float t=float(k)/segments,u=float(k+1)/segments;glm::vec3 p(std::sin(a)*(base+.2f*t*t),h*t,std::cos(a)*(base+.2f*t*t)),q(std::sin(a)*(base+.2f*u*u),h*u,std::cos(a)*(base+.2f*u*u));b.tri(p-side*(1-t),q-side*(1-u),p+side*(1-t),1+j%2);b.tri(p+side*(1-t),q-side*(1-u),q+side*(1-u),1+j%2);}
            if(type==PlantType::Reed&&!coarse)b.tube({std::sin(a)*(base+.2f),h-.18f,std::cos(a)*(base+.2f)},{std::sin(a)*(base+.2f),h+.05f,std::cos(a)*(base+.2f)},.014f,.01f,4,0);
        }
    }
    float height=0,base=0;for(const auto& v:b.mesh->vertices){height=std::max(height,v.position.y);base=std::min(base,v.position.y);}
    for(auto& v:b.mesh->vertices){v.position.y-=base;v.position/=(height-base);}
    return b.mesh;
}
std::shared_ptr<const ImageRGBA8> plantPalette(PlantType type){
    const bool flower=type==PlantType::Daisy||type==PlantType::Wildflower;
    std::array<glm::u8vec3,4> colors={glm::u8vec3(90,57,31),glm::u8vec3(47,91,31),glm::u8vec3(83,123,43),glm::u8vec3(112,145,52)};
    if(flower){colors[0]={222,158,28};colors[3]=type==PlantType::Daisy?glm::u8vec3(242,237,212):glm::u8vec3(157,83,195);}
    if(type==PlantType::Conifer){colors[1]={26,68,42};colors[2]={49,92,48};}
    auto image=std::make_shared<ImageRGBA8>();image->width=64;image->height=16;image->pixels.resize(64*16*4);
    for(int y=0;y<16;y++)for(int x=0;x<64;x++){auto c=colors[x/16];float shade=.9f+.1f*std::sin(y*.7f)*std::sin(y*.7f);for(int k=0;k<3;k++)image->pixels[(y*64+x)*4+k]=uint8_t(c[k]*shade);image->pixels[(y*64+x)*4+3]=255;}
    return image;
}
}
