#include "renderer/rhi/GpuSubdivision.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include <iostream>
#include <cmath>
namespace render {
void validateSubdivisionRhi(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir){
    if(!d->computeLimits().supported){std::cout<<"RHI compute subdivision deferred on this backend\n";return;}
    const std::vector<MeshVertex> v{{{-.8f,-.8f,.4f},{0,0,1},{0,1}},{{.8f,-.8f,.4f},{0,0,1},{1,1}},{{-.8f,.8f,.4f},{0,0,1},{0,0}}};GpuSubdivision subdivide(d,dir,v,{0,1,2},{1,1,{128,0,0,255}});auto check=[](bool ok,const char* error){if(!ok)throw std::runtime_error(error);};
    for(uint32_t level:{1,2,4,7,10}){subdivide.update(level,.05f);auto vertices=subdivide.readVertices();check(vertices.size()==level*level*3,"Subdivision output count wrong");
        double area=0;for(size_t i=0;i<vertices.size();i+=3){const auto a=vertices[i],b=vertices[i+1],c=vertices[i+2];const auto cross=glm::cross(b.position-a.position,c.position-a.position);check(cross.z>0,"Subdivision winding reversed");area+=cross.z*.5;for(const auto& x:{a,b,c}){check(std::abs(x.position.z-(.4f+128.f/255*.05f))<2e-6f,"Subdivision displacement differs from CPU interpolation");check(std::abs(x.position.x-(-.8f+1.6f*x.uv.x))<2e-6f && std::abs(x.position.y-(.8f-1.6f*x.uv.y))<2e-6f,"Subdivision barycentric interpolation invalid");check(glm::length(x.normal-glm::vec3(0,0,1))<1e-6f,"Subdivision normal changed");}}
        check(std::abs(area-1.28)<1e-5,"Subdivision contains gaps or overlaps");
    }
    MaterialDesc desc;desc.extension.settings.z=1;desc.parameters.albedoAlpha={1,0,0,1};auto material=std::make_shared<GpuMaterial>(d,desc);ForwardPbrRenderer renderer(d,dir,32,32,PbrPath::Scene);FrameData frame;renderer.render(frame,{{subdivide.mesh(),material,glm::mat4(1)}});auto output=renderer.readHDR();size_t lit=0;for(size_t i=0;i<output.size();i+=4)if(output[i]>.99f)++lit;check(lit>100,"Generated subdivision mesh was not drawn through indexed indirect RHI");subdivide.update(2,0);for(const auto& x:subdivide.readVertices())check(std::abs(x.position.z-.4f)<1e-6f,"Subdivision height update failed");
    std::cout<<"RHI compute subdivision levels 1..10, barycentric displacement, winding, area and indexed indirect draw passed\n";
}
}
