#include "metal/MetalBackend.h"
#include "metal/MetalDemo.h"
#include "component/Ocean.h"
#include "component/Atmosphere.h"
#include "component/Lights.h"
#include "component/GameObject.h"
#include "component/transform.h"
#include "component/Mesh_Renderer.h"
#include "component/Mesh_Filter.h"
#include "renderer/Material.h"
#include "renderer/Texture.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderPass.h"
#include "object/Terrain.h"
#include "object/SkyBox.h"
#include "system/RenderManager.h"
#include "system/InputManager.h"
#include "utils/Camera.h"
#include <glad/glad.h>
#include <complex>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
extern std::shared_ptr<RenderScene> scene;
namespace {
using Complex=std::complex<double>;
constexpr double pi=3.141592653589793;
int cases=0;
void check(bool ok,const char* name){if(!ok)throw std::runtime_error(std::string("Ocean GPU regression: ")+name);++cases;}
std::shared_ptr<ImageTexture> image(int n,const std::vector<glm::vec4>& values={}) {
    auto t=std::make_shared<ImageTexture>();t->genImageTexture(GL_RGBA32F,GL_RGBA,n,n);
    if(!values.empty()){glBindTexture(GL_TEXTURE_2D,t->tex->id);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,n,n,0,GL_RGBA,GL_FLOAT,values.data());}
    return t;
}
void dispatch(const std::shared_ptr<Shader>& shader,int n){shader->use();glDispatchCompute(n/8,n/8,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);}
std::vector<Complex> dft(const std::vector<Complex>& spectrum,int n) {
    std::vector<Complex> result(n*n);
    // Independent direct inverse DFT, not another butterfly implementation.
    for(int y=0;y<n;++y)for(int x=0;x<n;++x)
        for(int ky=0;ky<n;++ky)for(int kx=0;kx<n;++kx)
            result[y*n+x]+=spectrum[ky*n+kx]*std::polar(1.0,2*pi*(kx*x+ky*y)/n);
    return result;
}
void fftReference(int n,bool singleMode) {
    std::vector<glm::vec4> input(n*n);std::vector<Complex> spectral(n*n);
    for(int i=0;i<n*n;++i) {
        spectral[i]=singleMode?Complex(i==3?1:0,0):Complex(std::sin(i*.37),std::cos(i*.61));
        input[i]={spectral[i].real(),spectral[i].imag(),0,0};
    }
    auto a=image(n,input),b=image(n);
    const char* names[]={"Horizontal","Vertical"};
    for(int axis=0;axis<2;++axis)for(int ns=1;ns<n;ns*=2) {
        auto path=std::string("./src/shader/ocean/ocean_FFT")+names[axis]+(ns==n/2?"End":"")+".comp";
        auto shader=std::make_shared<Shader>(path.c_str());
        shader->use();shader->setInt("N",n);shader->setInt("Ns",ns);a->setBinding(5);b->setBinding(6);
        dispatch(shader,n);std::swap(a,b);
    }
    auto raw=MetalBackend::readFloatTexture(a->tex->id);std::vector<Complex> expected(n*n);
    if(singleMode)for(int y=0;y<n;++y)for(int x=0;x<n;++x)expected[y*n+x]=std::polar(1.0,2*pi*3*x/n);
    else expected=dft(spectral,n);
    double error=0,scale=1;
    for(int i=0;i<n*n;++i){error=std::max(error,std::abs(Complex(raw.rgba[4*i],raw.rgba[4*i+1])-expected[i]));scale=std::max(scale,std::abs(expected[i]));}
    std::cout<<"Ocean IFFT N="<<n<<" relative max error="<<error/scale<<'\n';
    check(error/scale<2e-5,"2D complex IFFT versus CPU direct DFT");
}
void conversion() {
    constexpr int n=8;std::vector<glm::vec4> data(n*n,glm::vec4(-64,200,0,0));
    auto y=image(n,data),x=image(n,data),z=image(n,data),out=image(n);
    auto shader=std::make_shared<Shader>("./src/shader/ocean/ocean_TextureGenerationDisplace.comp");
    shader->use();shader->setInt("N",n);shader->setFloat("HeightScale",2);shader->setFloat("Lambda",.5);
    y->setBinding(2);x->setBinding(3);z->setBinding(4);out->setBinding(7);dispatch(shader,n);
    auto raw=MetalBackend::readFloatTexture(out->tex->id);
    check(std::abs(raw.rgba[0]+.5)<1e-6 && std::abs(raw.rgba[1]+2)<1e-6 && std::abs(raw.rgba[2]+.5)<1e-6,"signed real displacement, normalization and scale");
    check(std::abs(raw.rgba[4]-.5)<1e-6 && std::abs(raw.rgba[5]-2)<1e-6,"centered-spectrum checkerboard exactly once");
}
void normalFoam(int n,bool flat) {
    constexpr float length=16;std::vector<glm::vec4> data(n*n);
    for(int z=0;z<n;++z)for(int x=0;x<n;++x)
        data[z*n+x]=flat?glm::vec4(0):glm::vec4(-.5*std::sin(2*pi*x/n),.5*std::sin(2*pi*z/n),0,0);
    auto d=image(n,data),normals=image(n),foam=image(n);
    auto shader=std::make_shared<Shader>("./src/shader/ocean/ocean_TextureGenerationNormalBubbles.comp");
    shader->use();shader->setInt("N",n);shader->setFloat("OceanLength",length);
    shader->setFloat("BubblesScale",2);shader->setFloat("BubblesThreshold",.86f);
    d->setBinding(7);normals->setBinding(5);foam->setBinding(6);dispatch(shader,n);
    auto nr=MetalBackend::readFloatTexture(normals->tex->id),fr=MetalBackend::readFloatTexture(foam->tex->id);
    double error=0;float unit=length/n;
    for(int z=0;z<n;++z)for(int x=0;x<n;++x) {
        float slope=flat?0: .5*std::sin(2*pi/n)*std::cos(2*pi*z/n)/unit;
        float compression=flat?0: -.5*std::sin(2*pi/n)*std::cos(2*pi*x/n)/unit;
        auto expected=glm::normalize(glm::vec3(0,1,-slope));int index=4*(z*n+x);
        for(int c=0;c<3;++c)error=std::max(error,double(std::abs(nr.rgba[index+c]-expected[c])));
        float J=1+compression;error=std::max(error,double(std::abs(fr.rgba[index+3]-J)));
        error=std::max(error,double(std::abs(fr.rgba[index]-std::clamp((.86f-J)*2,0.f,1.f))));
    }
    check(error<2e-5,flat?"flat normal and zero foam":"periodic Z normals and world-space Jacobian/foam");
}
void spectrumAndEvolution() {
    Ocean ocean;ocean.FFTPow=4;ocean.fft_size=16;ocean.MeshSize=17;ocean.MeshLength=64;ocean.WindScale=12;
    ocean.HeightScale=1;ocean.Lambda=.8;
    const float t=1.7f;ocean.simulate(t);
    auto random=MetalBackend::readFloatTexture(ocean.GaussianRandomRT_Texture->tex->id);
    auto heights=MetalBackend::readFloatTexture(ocean.HeightSpectrumRT_Texture->tex->id);
    std::vector<Complex> spectrum(256);int n=16;double dk=2*pi/64,L=144/9.81;
    for(int y=1;y<n;++y)for(int x=1;x<n;++x) {
        double kx=(x-n/2)*dk,kz=(y-n/2)*dk,k2=kx*kx+kz*kz;if(k2==0)continue;
        double alignment=(kx+kz)/std::sqrt(2*k2);
        double P=ocean.A*std::exp(-1/(k2*L*L))/(k2*k2)*alignment*alignment*std::exp(-k2*L*L*1e-6);
        int i=y*n+x,mirror=((n-y)%n)*n+(n-x)%n;
        auto a=Complex(random.rgba[4*i],random.rgba[4*i+1])*std::sqrt(P*.5);
        auto b=std::conj(Complex(random.rgba[4*mirror],random.rgba[4*mirror+1]))*std::sqrt(P*.5);
        double phase=std::sqrt(9.81*std::sqrt(k2))*t;
        spectrum[i]=(a*std::polar(1.0,phase)+b*std::polar(1.0,-phase))*(dk*n*n);
    }
    auto reference=dft(spectrum,n);double error=0,maxReal=0,maxImag=0;
    for(int i=0;i<n*n;++i){error=std::max(error,std::abs(Complex(heights.rgba[4*i],heights.rgba[4*i+1])-reference[i])/(n*n));
        maxReal=std::max(maxReal,double(std::abs(heights.rgba[4*i])));maxImag=std::max(maxImag,double(std::abs(heights.rgba[4*i+1])));}
    std::cout<<"Ocean spectrum/evolution CPU error="<<error<<" imaginary/real="<<maxImag/maxReal<<'\n';
    check(error<2e-5,"metre wave numbers, independent conjugate mode and dispersion versus CPU");
    check(maxImag/maxReal<2e-5,"Hermitian height field is real");
    auto first=MetalBackend::readFloatTexture(ocean.DisplaceRT_Texture->tex->id);
    double mean=0;float low=0,high=0;for(size_t i=1;i<first.rgba.size();i+=4){mean+=first.rgba[i];low=std::min(low,first.rgba[i]);high=std::max(high,first.rgba[i]);}
    check(std::abs(mean/(n*n))<1e-6 && low<0 && high>0,"zero mean with positive crests and negative troughs");
    ocean.simulate(t);auto second=MetalBackend::readFloatTexture(ocean.DisplaceRT_Texture->tex->id);
    check(first.rgba==second.rgba,"same seed/time is deterministic");
    ocean.simulate(t+.5f);second=MetalBackend::readFloatTexture(ocean.DisplaceRT_Texture->tex->id);
    check(first.rgba!=second.rgba,"time advances wave phases");
    ocean.seed+=1;ocean.simulate(t);second=MetalBackend::readFloatTexture(ocean.DisplaceRT_Texture->tex->id);
    check(first.rgba!=second.rgba,"changing seed regenerates Gaussian field");
    for(int mode=0;mode<3;++mode) {
        ocean.WindScale=mode==0?0:12;ocean.WindAndSeed=mode==1?glm::vec4(0):glm::vec4(1,1,0,0);ocean.A=mode==2?0:.0005f;
        ocean.simulate(t);auto raw=MetalBackend::readFloatTexture(ocean.DisplaceRT_Texture->tex->id);
        bool zero=true;for(size_t i=0;i<raw.rgba.size();i+=4)for(int c=0;c<3;++c)zero &= std::isfinite(raw.rgba[i+c]) && raw.rgba[i+c]==0;
        check(zero,"zero speed/direction/amplitude remains flat and finite");
    }
}
void surfaceLighting() {
    auto manager=RenderManager::GetInstance();auto savedScene=scene;auto savedSettings=manager->setting;
    scene=makeMetalOceanScene();scene->addSky({});
    auto water=std::static_pointer_cast<Ocean>(scene->terrain()->GetComponent("Ocean"));
    water->FFTPow=6;water->fft_size=64;water->MeshSize=65;water->animate=false;water->inner_time=8;
    auto frame=[&](){MetalBackend::beginFrame();manager->render(scene);auto raw=MetalBackend::readFloatTexture(manager->deferredPass->postTexture->id);MetalBackend::present();return raw;};
    manager->setting.enableDirectional=false;auto dark=frame();float darkMax=0;
    for(size_t i=0;i<dark.rgba.size();i+=4)for(int c=0;c<3;++c)darkMax=std::max(darkMax,std::abs(dark.rgba[i+c]));
    check(darkMax<1e-6,"no sky and disabled sun yields no stale or hardcoded lighting");
    manager->setting.enableDirectional=true;scene->directionLights()[0]->data.color={100,0,0};auto lit=frame();
    bool finite=true,onlyRed=true;float maxRed=0;
    for(size_t i=0;i<lit.rgba.size();i+=4){for(int c=0;c<4;++c)finite &= std::isfinite(lit.rgba[i+c]);maxRed=std::max(maxRed,lit.rgba[i]);onlyRed &= std::abs(lit.rgba[i+1])<1e-6 && std::abs(lit.rgba[i+2])<1e-6;}
    check(finite,"ocean shader output is finite without a sky");
    check(maxRed>1,"sun reflection stays HDR above one");
    check(onlyRed,"ocean respects scene sun color");
    scene=savedScene;manager->setting=savedSettings;
}
}
void validateMetalOcean() {
    cases=0;fftReference(8,true);fftReference(8,false);fftReference(16,false);fftReference(1024,true);
    conversion();normalFoam(16,true);normalFoam(16,false);normalFoam(32,false);spectrumAndEvolution();surfaceLighting();validateMetalWaterOptics();
    std::cout<<"Ocean analytic GPU tests passed ("<<cases<<" cases)\n";
}
std::shared_ptr<RenderScene> makeMetalOceanScene(bool clearWater) {
    auto result=makeMetalDemoScene();
    if(!clearWater) { auto all=result->objects();for(const auto& o:all)if(!o->getComponent<DirectionLight>())result->removeObject(o->assetId); }
    else for(size_t i=0;i<result->objects().size();++i) {
        auto object=result->objects()[i];
        auto renderer=std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
        if(!renderer)continue;
        object->setDeferred(true);
        auto trans=std::static_pointer_cast<Transform>(object->GetComponent("Transform"));
        trans->position={float(int(i)-2)*2.6f,-2.6f,-5};trans->scale=glm::vec3(1.4f);
        renderer->shader=RenderManager::GetInstance()->getShader(ShaderType::PBR);renderer->drawMode=GL_TRIANGLES;
    }
    {auto all=result->objects();for(const auto& o:all)if(o->getComponent<PointLight>() || o->getComponent<SpotLight>())result->removeObject(o->assetId);}
    result->setCamera(std::make_shared<Camera>(glm::vec3(0,7.5f,30),glm::vec3(0,1,0),-90,-10));result->mainCamera()->Zoom=58;result->mainCamera()->exposure=1;
    if(!result->directionLights().empty())result->directionLights()[0]->data.direction={0,-.17364818f,.98480775f};
    auto ocean=std::make_shared<Ocean>();ocean->FFTPow=10;ocean->fft_size=1024;ocean->MeshSize=513;
    ocean->MeshLength=256;ocean->seaLevel=0;
    // A rough deep-water preset: larger swell, steep crests and compression-driven whitecaps.
    ocean->WindScale=28;ocean->A=.0008f;ocean->HeightScale=1.8f;ocean->Lambda=1.15f;
    ocean->BubblesThreshold=.92f;ocean->BubblesScale=3;
    ocean->outer_OceanColorShallow={.2f,.75f,.85f};ocean->outer_OceanColorDeep={.035f,.28f,.35f};
    if(clearWater) {
        ocean->WindScale=9;ocean->A=.0005f;ocean->HeightScale=.6f;ocean->Lambda=.5f;ocean->refractionStrength=.35f;
        ocean->BubblesThreshold=.86f;ocean->BubblesScale=2;
        ocean->absorption={.08f,.025f,.012f};ocean->scattering={.01f,.02f,.025f};
        result->setCamera(std::make_shared<Camera>(glm::vec3(0,9,13),glm::vec3(0,1,0),-90,-38));result->mainCamera()->Zoom=58;
        result->directionLights()[0]->data.direction={0,-.5735764f,.8191520f};
        std::static_pointer_cast<Atmosphere>(result->sky()->GetComponent("Atmosphere"))->sunAngle=35;
        auto floor=std::make_shared<GameObject>();floor->name="Submerged sand";auto transform=std::make_shared<Transform>();floor->addComponent(transform);
        std::vector<Vertex> vertices(4);const glm::vec3 positions[]={{-45,-4,40},{45,-4,40},{45,-4,-50},{-45,-4,-50}};
        for(int i=0;i<4;++i){vertices[i]=Vertex{};vertices[i].Position=positions[i];vertices[i].Normal={0,1,0};vertices[i].TexCoords={float(i==1||i==2),float(i>=2)};}
        auto mesh=std::make_shared<Mesh>(vertices,std::vector<unsigned>{0,1,2,0,2,3});
        auto sample=std::static_pointer_cast<MeshFilter>(result->objects()[0]->GetComponent("MeshFilter"))->meshes[0]->material;
        auto mat=std::make_shared<Material>();mat->textures=sample->textures;mat->albedoFactor={2.4f,1.6f,.8f};mesh->material=mat;
        auto filter=std::make_shared<MeshFilter>();filter->addMesh(mesh);floor->addComponent(filter);
        auto renderer=std::make_shared<MeshRenderer>();renderer->shader=RenderManager::GetInstance()->getShader(ShaderType::PBR);floor->addComponent(renderer);floor->setDeferred(true);result->addObject(floor);
    }
    result->addTerrain(std::make_shared<Terrain>());result->terrain()->addComponent(ocean);
    auto manager=RenderManager::GetInstance();manager->setting.enableRSM=false;manager->setting.enableSSAO=false;
    manager->setting.useDefer=true;manager->setting.enableDirectional=true;
    return result;
}
