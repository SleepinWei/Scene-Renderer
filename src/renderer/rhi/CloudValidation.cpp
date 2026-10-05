#include "renderer/rhi/GpuClouds.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "component/Cloud.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <limits>
namespace render {
namespace {void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}}
void validateCloudsRhi(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    if(!device->computeLimits().maxStorageImages)return;
    Cloud component;const auto original=component.settings();auto invalid=original;invalid.density=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;try{component.setSettings(invalid);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected && component.settings()==original,"Invalid clouds partially published component settings");
    json config={{"enabled",true},{"coverage",.7f},{"wind",{5,9}},{"downsample",4}};component.loadFromJson(config);
    check(component.settings().enabled && component.settings().downsample==4 && component.settings().wind==glm::vec2(5,9),"Cloud JSON settings were not captured");
    ForwardPbrRenderer renderer(device,directory,80,60,PbrPath::Scene);
    FrameData f;f.sky=true;f.sunAngle=35;f.clouds.enabled=true;f.clouds.coverage=.85f;f.clouds.steps=32;f.clouds.lightSteps=3;
    f.viewportWidth=80;f.viewportHeight=60;f.cameraPosition={0,10,0};f.nearPlane=.1f;f.farPlane=100000;f.timeSeconds=8;
    auto camera=[&](glm::vec3 direction,float fov=50.f){f.view=glm::lookAt(f.cameraPosition,f.cameraPosition+glm::normalize(direction),glm::vec3(0,1,0));glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;f.viewProjection=correction*glm::perspective(glm::radians(fov),float(f.viewportWidth)/f.viewportHeight,f.nearPlane,f.farPlane)*f.view;};
    camera({0,.7f,-1});renderer.render(f,{});auto counts=renderer.cloudTileCounts();auto values=renderer.readClouds();auto meta=renderer.readCloudMetadata();
    check(counts[0]>0 && counts[0]<=counts[1],"Cloud GPU tile queue count outside capacity");
    double opacity=0;for(size_t i=0;i<values.size();i+=4){for(int c=0;c<4;c++)check(std::isfinite(values[i+c]),"Cloud output nonfinite");check(values[i+3]>=0 && values[i+3]<=1,"Cloud transmittance outside 0..1");opacity+=1-values[i+3];check(meta[i+2]<=f.clouds.steps,"Cloud primary raymarch exceeded budget");}
    check(opacity>1,"Dense clouds did not attenuate the sky");
    f.timeSeconds+=.05f;renderer.render(f,{});for(float v:renderer.readClouds())check(std::isfinite(v),"Wind reprojection produced nonfinite history");
    camera({0,.7f,-1},65.f);renderer.render(f,{});auto projectionCut=renderer.readClouds();
    renderer.resetTemporal();renderer.render(f,{});check(renderer.readClouds()==projectionCut,"Cloud FOV cut retained temporal history");
    f.clouds.coverage=0;renderer.render(f,{});check(renderer.cloudTileCounts()[0]==0,"Zero coverage failed GPU dispatch culling");
    values=renderer.readClouds();for(size_t i=0;i<values.size();i+=4)check(values[i]==0 && values[i+1]==0 && values[i+2]==0 && values[i+3]==1,"Zero work retained stale cloud history");
    f.clouds.coverage=.85f;f.clouds.density=0;renderer.render(f,{});check(renderer.cloudTileCounts()[0]==0,"Zero density failed GPU dispatch culling");f.clouds.density=.0018f;
    camera({0,-1,-1});renderer.render(f,{});check(renderer.cloudTileCounts()[0]==0,"Clouds rendered through the planet below horizon");
    f.cameraPosition.y=2000;camera({0,.4f,-1});renderer.render(f,{});check(renderer.cloudTileCounts()[0]>0,"Inside-cloud shell intersection lost all rays");
    f.cameraPosition.y=6000;camera({0,-1,-1});renderer.render(f,{});check(renderer.cloudTileCounts()[0]>0,"Above-cloud view lost the front shell segment");
    f.cameraPosition.y=10;camera({0,.7f,-1});const auto inverseView=glm::inverse(f.view);
    glm::vec3 forward=-glm::vec3(inverseView[2]),right=glm::vec3(inverseView[0]),up=glm::vec3(inverseView[1]),center=f.cameraPosition+forward*10.f;
    std::vector<MeshVertex> vertices;for(auto p:{center-right*20.f-up*20.f,center+right*20.f-up*20.f,center+right*20.f+up*20.f,center-right*20.f+up*20.f})vertices.push_back({p,-forward,{0,0}});
    auto mesh=std::make_shared<GpuMesh>(device,vertices,std::vector<uint32_t>{0,1,2,0,2,3});MaterialDesc material;material.parameters.albedoAlpha={.2f,.4f,.3f,1};material.parameters.emissiveNormal.w=0;
    auto gpuMaterial=std::make_shared<GpuMaterial>(device,material);DrawPacket packet{mesh,gpuMaterial};
    renderer.render(f,{packet});check(renderer.cloudTileCounts()[0]==0,"Opaque foreground did not cull cloud tiles");auto blocked=renderer.readHDR();
    f.clouds.enabled=false;renderer.render(f,{packet});auto clear=renderer.readHDR();check(blocked==clear,"Cloud composite altered fully occluded geometry");
    renderer.resize(65,49);f.viewportWidth=65;f.viewportHeight=49;f.clouds.enabled=true;f.clouds.downsample=4;camera({0,.7f,-1});renderer.render(f,{});
    check(renderer.readClouds().size()==17*13*4 && renderer.cloudTileCounts()[1]==6,"Odd cloud viewport or resize lost edge tiles");
    f.clouds.seed++;renderer.render(f,{});for(float v:renderer.readClouds())check(std::isfinite(v),"Cloud seed invalidation produced nonfinite output");
    std::cout<<"RHI GPU-driven clouds: compact indirect queue, zero-work clear, depth occlusion, inside/above shell, wind history, seed and odd resize passed; initial summed opacity "<<opacity<<"\n";
}
}
