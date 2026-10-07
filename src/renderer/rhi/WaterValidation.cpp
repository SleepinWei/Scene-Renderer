#include "renderer/rhi/ForwardPbrRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdlib>
// The standalone Vulkan validation target does not link the asset importer.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include "renderer/rhi/GpuShoreWater.h"
#include "renderer/rhi/GpuVirtualTexture.h"
#include "renderer/rhi/WaterTransport.h"
#include "renderer/rhi/GpuWaterCaustics.h"
#include "renderer/rhi/GpuWaterSunShafts.h"
namespace render {
namespace {
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
std::shared_ptr<GpuMesh> waterQuad(std::shared_ptr<rhi::GraphicsDevice> d,float y,float size){
    return std::make_shared<GpuMesh>(d,std::vector<MeshVertex>{{{-size,y,-size},{0,1,0},{0,0}},{{-size,y,size},{0,1,0},{0,1}},{{size,y,size},{0,1,0},{1,1}},{{size,y,-size},{0,1,0},{1,0}}},std::vector<uint32_t>{0,1,2,0,2,3});
}
void validateSunShafts(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory){
    using namespace rhi;Resources resources(d);OceanSurfaceSettings s;s.seaLevel=0;s.spectrum.length=32;s.surfaceLength=512;s.detailWaves=false;
    auto texture=[&](uint32_t n,const std::vector<glm::vec4>& data){auto t=resources.texture({n,n,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Sun shaft validation field"});d->writeTextureFloat(t,reinterpret_cast<const float*>(data.data()),data.size()*16);return t;};
    std::vector<glm::vec4> wave(256*256,glm::vec4(0)),normals(256*256,{0,1,0,0});
    auto displacement=texture(256,wave),normal=texture(256,normals),zero=texture(1,{{0,0,0,0}}),mask=texture(1,{{1,1,1,1}});
    std::array<TextureViewHandle,6> fft={resources.view(displacement),resources.view(normal),resources.view(zero),resources.view(zero),resources.view(normal),resources.view(zero)};
    GpuWaterSunShafts shafts(d,directory);
    auto run=[&](glm::vec3 sun,glm::vec3 camera=glm::vec3(0,-6,0)){Resources frame(d);auto c=d->createCommandList();shafts.record(frame,c,s,camera,glm::normalize(sun),fft,resources.view(zero),glm::vec4(0,0,1,1),resources.view(mask));d->submit(c);return shafts.read();};
    auto stats=[&](const std::vector<float>& image,int slice){glm::vec3 result(0,100,0);for(int y=32;y<224;++y)for(int x=32;x<224;++x){float v=image[4*(y*2048+slice*256+x)];require(std::isfinite(v),"Nonfinite underwater solar flux");result.x+=v;result.y=std::min(result.y,v);result.z=std::max(result.z,v);}result.x/=192*192;return result;};
    auto flat=run({.4,1,.2});for(int i=1;i<8;++i){auto v=stats(flat,i);require(v.y>.995&&v.z<1.005,"Flat underwater shaft field changed uniform solar energy");}
    s.absorption={2,1,.5};auto absorbing=run({.4,1,.2});require(absorbing==flat,"Volume flux field double-counted Beer extinction");s.absorption=glm::vec3(0);
    auto waves=[&](float phase){for(int y=0;y<256;++y)for(int x=0;x<256;++x){float wx=(float(x)/256-.5f)*32,wz=(float(y)/256-.5f)*32,a=wx*glm::two_pi<float>()/4+phase,b=wz*glm::two_pi<float>()/5+phase*.7f;int i=y*256+x;
        wave[i]={0,.12f*std::cos(a)+.08f*std::cos(b),0,0};normals[i]=glm::vec4(glm::normalize(glm::vec3(.12f*glm::two_pi<float>()/4*std::sin(a),1,.08f*glm::two_pi<float>()/5*std::sin(b))),0);}
        d->writeTextureFloat(displacement,reinterpret_cast<const float*>(wave.data()),wave.size()*16);d->writeTextureFloat(normal,reinterpret_cast<const float*>(normals.data()),normals.size()*16);};
    waves(0);auto focused=run({.4,1,.2});auto v=stats(focused,3);require(std::abs(v.x-1)<.06&&v.y<.9&&v.z>1.1,"Wave-volume solar rays lost energy or produced no focusing");
    waves(.6f);auto later=run({.4,1,.2});double change=0;for(int y=32;y<224;++y)for(int x=32;x<224;++x){int i=4*(y*2048+3*256+x);change+=std::abs(later[i]-focused[i]);}change/=192*192;require(change>.015,"Underwater sun shafts did not follow changing waves");
    auto night=run({.4,-1,.2});for(size_t i=0;i<night.size();i+=4)require(night[i]==0,"Night sun produced volumetric solar flux");
    const glm::vec4 dry(0);d->writeTextureFloat(mask,&dry.x,16);auto masked=run({0,1,0});for(size_t i=0;i<masked.size();i+=4)require(masked[i]==0,"Dry surface emitted underwater sun shafts");
    std::cout<<"Sun shafts: eight-depth flat energy, single extinction, wave focusing mean/min/max "<<v.x<<"/"<<v.y<<"/"<<v.z<<", motion "<<change<<", night/dry exclusion and no seabed dependency passed\n";
}

void validateCaustics(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory) {
    using namespace rhi;Resources resources(d);OceanSurfaceSettings s;s.seaLevel=0;s.detailWaves=true;
    s.bathymetryModel=glm::scale(glm::mat4(1),glm::vec3(64,1,64));s.absorption=s.scattering=glm::vec3(0);
    auto texture=[&](uint32_t n,const std::vector<glm::vec4>& data){auto t=resources.texture({n,n,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Caustic validation field"});d->writeTextureFloat(t,reinterpret_cast<const float*>(data.data()),data.size()*16);return t;};
    auto bed=texture(2,std::vector<glm::vec4>(4,{-2,.5,.5,.5})),zero=texture(1,{{0,0,0,0}}),normal=texture(1,{{0,1,0,0}}),mask=texture(1,{{1,1,1,1}});
    std::vector<glm::vec4> wave(256*256,glm::vec4(0)),normals(256*256,{0,1,0,0});auto detail=texture(256,wave),detailNormal=texture(256,normals);
    std::array<TextureViewHandle,6> fft={resources.view(zero),resources.view(normal),resources.view(zero),resources.view(detail),resources.view(detailNormal),resources.view(zero)};
    GpuWaterCaustics caustics(d,directory);
    auto run=[&](glm::vec3 sun,glm::vec3 camera=glm::vec3(0)){Resources frame(d);auto command=d->createCommandList();caustics.record(frame,command,s,camera,glm::normalize(sun),fft,resources.view(bed),resources.view(zero),glm::vec4(0),resources.view(mask));d->submit(command);return caustics.read();};
    auto stats=[&](const std::vector<float>& data){glm::vec3 result(0,100,0);size_t count=0;for(int y=32;y<480;++y)for(int x=32;x<480;++x){auto i=4*(y*512+x);for(int c=0;c<4;++c)require(std::isfinite(data[i+c]),"Nonfinite caustic output");require(std::abs(data[i+3]+2)<.01,"Caustic receiver height mismatch");result.x+=data[i];result.y=std::min(result.y,data[i]);result.z=std::max(result.z,data[i]);++count;}result.x/=count;return result;};
    auto flat=stats(run({0,1,0}));require(flat.y>.997&&flat.z<1.003,"Flat caustics altered uniform solar flux");
    for(uint32_t cascade=1;cascade<3;++cascade){auto uniform=stats(caustics.read(cascade));require(uniform.y>.99&&uniform.z<1.01,"Outer caustic cascade changed uniform solar flux");}

    s.absorption={.2,.1,.05};auto oblique=stats(run({.4,1,.2}));require(oblique.y>.99&&oblique.z<1.01,"Caustics double-counted oblique Fresnel or Beer attenuation");const glm::vec4 raised(0,.5,0,0),cleared(0);d->writeTextureFloat(zero,&raised.x,16);auto raisedSurface=stats(run({0,1,0}));require(raisedSurface.y>.997&&raisedSurface.z<1.003,"Caustic normalization repeated extinction when the macro water level changed");d->writeTextureFloat(zero,&cleared.x,16);s.absorption=glm::vec3(0);
    auto waves=[&](float phase){for(int y=0;y<256;++y)for(int x=0;x<256;++x){float wx=(float(x)/256-.5f)*32,wz=(float(y)/256-.5f)*32;float a=wx*glm::two_pi<float>()/1.28f+phase,b=wz*glm::two_pi<float>()/1.6f+phase*.7f;auto i=y*256+x;wave[i]={0,.004f*std::cos(a)+.003f*std::cos(b),0,0};auto n=glm::normalize(glm::vec3(.004f*glm::two_pi<float>()/1.28f*std::sin(a),1,.003f*glm::two_pi<float>()/1.6f*std::sin(b)));normals[i]=glm::vec4(n,0);}d->writeTextureFloat(detail,reinterpret_cast<const float*>(wave.data()),wave.size()*16);d->writeTextureFloat(detailNormal,reinterpret_cast<const float*>(normals.data()),normals.size()*16);};
    waves(0);auto first=run({0,1,0});auto focused=stats(first);require(std::abs(focused.x-1)<.03&&focused.y<.94&&focused.z>1.06,"Wave focusing lost flux or produced no caustics");
    auto moved=run({0,1,0},{3.f/32,0,0});double scrolling=0;for(int y=32;y<480;++y)for(int x=32;x<477;++x)scrolling=std::max(scrolling,double(std::abs(moved[4*(y*512+x)]-first[4*(y*512+x+3)])));require(scrolling<.003,"Camera scrolling shifted the caustic photon lattice in world space");
    waves(.5);auto next=run({0,1,0});double motion=0;for(int y=32;y<480;++y)for(int x=32;x<480;++x)motion+=std::abs(next[4*(y*512+x)]-first[4*(y*512+x)]);motion/=448*448;require(motion>.005,"Caustics did not follow changing wave geometry");
    const glm::vec4 masked(0);d->writeTextureFloat(mask,&masked.x,16);auto dry=run({0,1,0});for(size_t i=0;i<dry.size();i+=4)require(dry[i]==1&&dry[i+3]>9000,"Unavailable dry caustic footprint darkened the bed");
    std::vector<glm::vec4> island(64*64,glm::vec4(0));for(int y=0;y<24;++y)for(int x=40;x<64;++x)island[y*64+x]=glm::vec4(1);mask=texture(64,island);s.surfaceLength=32;std::fill(wave.begin(),wave.end(),glm::vec4(0));std::fill(normals.begin(),normals.end(),glm::vec4(0,1,0,0));d->writeTextureFloat(detail,reinterpret_cast<const float*>(wave.data()),wave.size()*16);d->writeTextureFloat(detailNormal,reinterpret_cast<const float*>(normals.data()),normals.size()*16);auto partial=run({0,1,0});for(int y=256;y<352;++y)for(int x=256;x<352;++x){int i=4*(y*512+x);require(partial[i]==1&&partial[i+3]>9000,"Partially missing photon triangles projected a false dark footprint");}
    // A submerged opaque plane is a receiver above the authored bed. Its
    // irradiance must remain one for a flat wave surface in all three levels.
    MaterialDesc whiteDesc;whiteDesc.parameters.albedoAlpha={1,1,1,1};auto white=std::make_shared<GpuMaterial>(d,whiteDesc);
    std::vector<DrawPacket> receivers{{waterQuad(d,-1,80),white,glm::mat4(1)}};
    const glm::vec4 wet(1);mask=texture(1,{wet});s.surfaceLength=256;
    auto receiverRun=[&](){Resources frame(d);auto command=d->createCommandList();caustics.record(frame,command,s,glm::vec3(0,s.seaLevel,0),glm::normalize(glm::vec3(.4,1,.2)),fft,resources.view(bed),resources.view(zero),glm::vec4(0),resources.view(mask),receivers);d->submit(command);return caustics.read();};
    auto plane=receiverRun();for(uint32_t cascade=0;cascade<3;++cascade){auto data=cascade?caustics.read(cascade):plane;double error=0;for(int y=96;y<416;++y)for(int x=96;x<416;++x){int i=4*(y*512+x);require(std::abs(data[i+3]+1)<.015,"Opaque mesh was not a caustic receiver");error=std::max(error,double(std::abs(data[i]-1)));}require(error<.025,"Mesh receiver normalization repeated Fresnel or altered uniform energy");}
    s.seaLevel=1000;s.bathymetryModel[3].y=1000;receivers[0].model=glm::translate(glm::mat4(1),glm::vec3(0,1000,0));auto elevated=receiverRun();
    require(std::abs(elevated[4*(256*512+256)+3]+1)<.015&&std::abs(elevated[4*(256*512+256)]-1)<.025,"Caustic receiver height lost precision at elevated sea level");
    s.seaLevel=0;s.bathymetryModel[3].y=0;receivers[0].model=glm::mat4(1);
    const glm::vec4 deepBed(-10,.5,.5,.5);std::vector<glm::vec4> deep(4,deepBed);d->writeTextureFloat(bed,reinterpret_cast<const float*>(deep.data()),deep.size()*16);
    receivers[0].mesh=waterQuad(d,0,8);receivers[0].model=glm::translate(glm::mat4(1),glm::vec3(0,-3,0))*glm::rotate(glm::mat4(1),glm::radians(30.f),glm::vec3(0,0,1));
    auto tilted=receiverRun();const auto slope=caustics.projection();double tiltError=0;
    for(int y=224;y<288;++y)for(int x=224;x<288;++x){int i=4*(y*512+x);float sx=-8+(float(x)+.5f)/512*16,expected=(-3+std::tan(glm::radians(30.f))*sx)/(1-std::tan(glm::radians(30.f))*slope.x);
        require(std::abs(tilted[i+3]-expected)<.025,"Sun-aligned capture did not recover a sloping mesh receiver");tiltError=std::max(tiltError,double(std::abs(tilted[i]-1)));}require(tiltError<.03,"Tilted mesh caustics did not conserve flat solar flux");
    std::vector<MeshVertex> wall={{{0,-6,-4},{1,0,0},{0,0}},{{0,-2,-4},{1,0,0},{0,1}},{{0,-2,4},{1,0,0},{1,1}},{{0,-6,4},{1,0,0},{1,0}}};
    receivers[0].mesh=std::make_shared<GpuMesh>(d,wall,std::vector<uint32_t>{0,1,2,0,2,3});receivers[0].model=glm::mat4(1);auto vertical=receiverRun();
    int wallX=int((4*slope.x+8)/16*512),wallY=int((4*slope.y+8)/16*512),wallPixel=4*(wallY*512+wallX);
    require(std::abs(vertical[wallPixel+3]+4)<.1&&std::abs(vertical[wallPixel]-1)<.03,"Oblique sunlight did not reach a vertical caustic receiver");
    const glm::vec4 originalBed(-2,.5,.5,.5);std::vector<glm::vec4> original(4,originalBed);d->writeTextureFloat(bed,reinterpret_cast<const float*>(original.data()),original.size()*16);receivers[0].mesh=waterQuad(d,-1,80);
    s.causticMeshReceivers=false;auto terrainOnly=receiverRun();require(std::abs(terrainOnly[4*(256*512+256)+3]+2)<.01,"Disabling mesh receivers left stale receiver depth");s.causticMeshReceivers=true;
    MaterialDesc cutoutDesc=whiteDesc;cutoutDesc.parameters.albedoAlpha.w=0;cutoutDesc.parameters.factors.w=.5;receivers[0].material=std::make_shared<GpuMaterial>(d,cutoutDesc);
    auto cutout=receiverRun();require(std::abs(cutout[4*(256*512+256)+3]+2)<.01,"Alpha-cutout geometry incorrectly received caustics");receivers[0].material=white;
    waves(0);auto receiverWave=receiverRun();waves(.5);auto receiverMoved=receiverRun();double receiverMotion=0;for(int y=64;y<448;++y)for(int x=64;x<448;++x)receiverMotion+=std::abs(receiverMoved[4*(y*512+x)]-receiverWave[4*(y*512+x)]);receiverMotion/=384*384;
    require(receiverMotion>.002,"Mesh caustics did not follow the wave geometry");
    s.causticCascades=false;receiverRun();require(caustics.cascadeCount()==1,"Caustic cascade switch did not disable outer levels");s.causticCascades=true;
    std::cout<<"Cascaded caustics: 16/48/128 m flat energy, opaque/sloping/vertical mesh height/energy, elevated sea level, alpha cutouts, mesh/cascade switches and wave motion "<<receiverMotion<<" passed\n";
    auto night=run({0,-1,0});for(size_t i=0;i<night.size();i+=4)require(night[i]==1&&night[i+1]==1&&night[i+2]==1&&night[i+3]>9000,"Night caustics left stale focusing");
    std::cout<<"Caustics: flat "<<flat.x<<", oblique "<<oblique.x<<", wave mean/range "<<focused.x<<" / "<<focused.y<<".."<<focused.z<<", phase difference "<<motion<<'\n';
}
void validateCoastEdges(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory) {
    using namespace rhi;Resources resources(d);
    struct alignas(16) Probe {glm::mat4 inverse;glm::vec4 bed,patch;};Probe parameters{glm::mat4(1),{1,0,0,0},{-8,-8,16,.5}};
    std::vector<float> state(32*32*4);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){const auto i=(y*32+x)*4;state[i]=x<16?1:0;state[i+3]=x<16?-1:20;}
    auto texture=resources.texture({32,32,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Wet/dry sentinel fixture"});d->writeTextureFloat(texture,state.data(),state.size()*sizeof(float));
    auto output=resources.texture({8,1,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::CopySource,"Coast edge elevation"});auto sampler=resources.sampler({Filter::Linear,AddressMode::ClampToEdge});
    auto input=resources.buffer({sizeof(parameters),BufferUsage::Uniform,"Coast edge parameters"},&parameters);
    BindingLayout layout{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"CoastProbe",sizeof(parameters)},{1,BindingType::SampledTexture,ShaderStage::Compute,"probeState",0},{2,BindingType::StorageTextureWrite,ShaderStage::Compute,"probeOutput",0}}};
    auto path=directory+"/water-coast-validation.comp";auto pipeline=resources.computePipeline({{path+".glsl",path+".metallib",path+".spv",path+".json","main0"},{layout},{8,1,1},"Coast boundary regression"});
    auto set=resources.bindings({layout,{{0,input,0,sizeof(parameters),{},{}},{1,{},0,0,resources.view(texture),sampler},{2,{},0,0,resources.view(output),{}}}});
    auto commands=d->createCommandList();commands.dispatch(pipeline,{set},{1,1,1});d->submit(commands);auto values=d->readTextureFloat(output);
    for(int i=0;i<8;++i)require(std::abs(values[i*4]-(i<5?0.f:.125f))<1e-5,"Dry/out-of-bathymetry cells raised the rendered water surface");
    std::cout<<"Coast boundary: wet/dry interpolation and finite bathymetry/patch edges do not form water walls\n";
}
void validateShore(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory) {
    Resources resources(d);using namespace rhi;
    OceanSurfaceSettings s;s.seaLevel=0;s.bathymetryModel=glm::scale(glm::mat4(1),glm::vec3(8,1,8));
    s.shore.enabled=true;s.shore.resolution=32;s.shore.length=16;s.shore.swellHeight=0;s.shore.boundaryForcing=false;
    auto bed=resources.texture({33,33,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Coast validation bed"}),fft=resources.texture({1,1,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Zero FFT boundary"});
    auto bedView=resources.view(bed),fftView=resources.view(fft);const glm::vec4 zero(0);d->writeTextureFloat(fft,&zero.x,16);
    std::vector<glm::vec4> heights(33*33);for(uint32_t y=0;y<33;++y)for(uint32_t x=0;x<33;++x)heights[y*33+x]={-1.f+.5f*std::sin(x*.16f)*std::cos(y*.11f),.5,.5,.5};
    d->writeTextureFloat(bed,reinterpret_cast<const float*>(heights.data()),heights.size()*16);
    GpuShoreWater resting(d,directory,s,bedView);resting.simulate(0,s,glm::vec3(0),fftView,fftView);resting.simulate(1,s,glm::vec3(0),fftView,fftView);
    auto state=resting.read();float restError=0,momentum=0;
    for(size_t i=0;i<state.size();i+=4){restError=std::max(restError,std::abs(state[i]+state[i+3]));momentum=std::max(momentum,std::abs(state[i+1])+std::abs(state[i+2]));}
    require(restError<.00002f&&momentum<.0002f,"Shallow water generated waves in a lake at rest");
    auto before=state;resting.simulate(1,s,glm::vec3(.5,0,0),fftView,fftView);state=resting.read();
    for(int y=0;y<32;++y)for(int x=0;x<31;++x)for(int c=0;c<4;++c)
        require(std::abs(state[(y*32+x)*4+c]-before[(y*32+x+1)*4+c])<.00002,"Shore patch scroll lost overlapping world state");
    for(auto& h:heights)h.x=-2;d->writeTextureFloat(bed,reinterpret_cast<const float*>(heights.data()),heights.size()*16);
    s.shore.swellHeight=.2f;s.shore.swellPeriod=4;GpuShoreWater closed(d,directory,s,bedView);closed.simulate(0,s,glm::vec3(0),fftView,fftView);
    auto mass=[](const std::vector<float>& a){double value=0;for(size_t i=0;i<a.size();i+=4){require(std::isfinite(a[i])&&a[i]>=0,"Negative or nonfinite shallow water depth");value+=a[i];}return value;};
    double massBefore=mass(closed.read());closed.simulate(1,s,glm::vec3(0),fftView,fftView);double massAfter=mass(closed.read());
    require(std::abs(massAfter/massBefore-1)<.0001,"Closed shallow-water fixture did not conserve mass");
    s.shore.boundaryForcing=true;s.shore.swellHeight=.45f;s.shore.swellPeriod=3.5f;s.shore.foamStrength=2;
    for(uint32_t y=0;y<33;++y)for(uint32_t x=0;x<33;++x)heights[y*33+x].x=.18f*(float(y)/32*16-8);
    d->writeTextureFloat(bed,reinterpret_cast<const float*>(heights.data()),heights.size()*16);
    GpuShoreWater beach(d,directory,s,bedView);size_t peakRunup=0,peakResidual=0;float peakFoam=0;
    for(int frame=0;frame<=32;++frame){beach.simulate(float(frame)*.25f,s,glm::vec3(0),fftView,fftView);auto q=beach.read(),foam=beach.read(true);mass(q);
        size_t runup=0,residual=0;for(size_t i=0;i<q.size();i+=4){require(std::isfinite(foam[i])&&foam[i]>=0&&foam[i]<=1&&foam[i+1]>=0&&foam[i+1]<=1,"Invalid shore foam/wetness history");
            if(foam[i+3]>0&&q[i]>.01)++runup;if(foam[i+3]>0&&q[i]<.008&&foam[i+1]>.1)++residual;peakFoam=std::max(peakFoam,foam[i]);}
        peakRunup=std::max(peakRunup,runup);peakResidual=std::max(peakResidual,residual);
    }
    require(peakRunup>16&&peakResidual>16&&peakFoam>.05,"Coastal simulation did not produce runup, retreat, persistent foam and wet sand");
    std::cout<<"Shore water: lake-at-rest error "<<restError<<", momentum "<<momentum<<", closed mass ratio "<<massAfter/massBefore<<", scrolling, wet/dry positivity; peak runup "<<peakRunup<<", residual wet sand "<<peakResidual<<", foam "<<peakFoam<<"\n";
}
void validateDiveVolume(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory) {
    FrameData f;f.cameraPosition={0,-1,0};f.ambient=0;f.toneMapping=false;f.viewportWidth=f.viewportHeight=128;
    f.lights={{{0,0,0,0},{3,3,3,0},{0,-1,0,0}}};
    OceanSurfaceSettings s;s.spectrum.size=16;s.spectrum.length=16;s.spectrum.amplitude=0;s.meshSize=33;s.surfaceLength=64;
    s.seaLevel=0;s.detailWaves=false;s.cameraGrid=false;s.absorption={.13,.032,.018};s.scattering={.012,.035,.048};s.underwaterVolumeSteps=16;
    f.oceans={s};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0,-8,0),glm::vec3(0,0,-1));glm::mat4 depth(1);depth[2][2]=.5;depth[3][2]=.5;
    f.viewProjection=depth*glm::perspective(glm::radians(65.f),1.f,.01f,100.f)*f.view;
    MaterialDesc material;material.parameters.albedoAlpha={0,0,0,1};material.parameters.factors={0,1,1,0};
    auto black=std::make_shared<GpuMaterial>(d,material);std::vector<DrawPacket> packets{{waterQuad(d,-8,40),black,glm::mat4(1)}};
    ForwardPbrRenderer renderer(d,directory,128,128,PbrPath::Scene);
    auto render=[&](){renderer.render(f,packets);auto image=renderer.readHDR();for(float v:image)require(std::isfinite(v),"Dive volume produced nonfinite radiance");return image;};
    auto difference=[](const auto& a,const auto& b){double sum=0;for(size_t i=0;i<a.size();i+=4)for(int c=0;c<3;++c)sum+=std::abs(a[i+c]-b[i+c]);return sum/(a.size()/4*3);};
    auto clear=render();f.oceans[0].underwaterSunShafts=true;auto flatShafts=render();require(difference(clear,flatShafts)<2e-5,"Flat sun shaft field changed integrated medium or requires seabed caustics");
    f.oceans[0].sunShaftStrength=0;auto zeroShafts=render();require(difference(clear,zeroShafts)<1e-7,"Zero shaft contrast changed integrated medium");
    f.oceans[0].underwaterSunShafts=false;f.oceans[0].sunShaftStrength=1;f.oceans[0].underwaterParticles=true;f.oceans[0].particleDensity=0;auto zero=render();require(difference(clear,zero)<1e-7,"Zero particle occupancy altered the medium");
    f.oceans[0].particleDensity=1;auto dust=render();require(difference(clear,dust)>1e-5,"Suspended sediment did not affect a submerged view");
    auto repeat=render();require(difference(dust,repeat)<1e-7,"World-space sediment is not deterministic");
    f.timeSeconds=10;auto moved=render();require(difference(dust,moved)>1e-5,"Suspended sediment failed to drift with simulation time");
    packets[0].mesh=waterQuad(d,-1.04f,40);auto blocked=render();f.oceans[0].underwaterParticles=false;auto blockedClear=render();require(difference(blocked,blockedClear)<1e-7,"Sediment behind opaque geometry leaked through the depth clip");
    std::cout<<"Dive volume: finite 16-sample integration, particle off/zero density, deterministic world placement, temporal drift and opaque occlusion passed\n";
}
void validateUnderwater(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory) {
    FrameData f;f.cameraPosition={0,-1,0};f.ambient=0;f.toneMapping=false;f.viewportWidth=f.viewportHeight=64;
    f.lights={{{0,0,0,0},{0,0,0,0},{0,-1,0,0}}};
    OceanSurfaceSettings s;s.spectrum.size=16;s.spectrum.length=16;s.spectrum.amplitude=0;s.meshSize=33;s.surfaceLength=64;
    s.seaLevel=0;s.detailWaves=false;s.cameraGrid=false;s.robustRefraction=true;s.absorption=s.scattering=glm::vec3(0);
    s.fresnel=0;s.specular=glm::vec3(0);s.anisotropy=0;f.oceans={s};
    glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    auto pointCamera=[&](glm::vec3 target){f.view=glm::lookAt(f.cameraPosition,target,glm::vec3(0,0,-1));
        f.viewProjection=depth*glm::perspective(glm::radians(20.f),1.f,.1f,100.f)*f.view;};
    MaterialDesc material;material.parameters.albedoAlpha={0,0,0,1};material.parameters.factors={0,1,1,0};material.parameters.emissiveNormal={.25f,.15f,.08f,0};
    auto glow=std::make_shared<GpuMaterial>(d,material);std::vector<DrawPacket> packets{{waterQuad(d,-2,40),glow,glm::mat4(1)}};
    ForwardPbrRenderer renderer(d,directory,64,64,PbrPath::Scene);const size_t centre=4*(32*64+32);
    auto render=[&](){renderer.render(f,packets);auto image=renderer.readHDR();for(float v:image)require(std::isfinite(v),"Underwater HDR became nonfinite");return image;};
    pointCamera({0,-2,0});auto clear=render();
    for(int c=0;c<3;++c)require(std::abs(clear[centre+c]-material.parameters.emissiveNormal[c])<.002,"Zero-extinction underwater view changed the floor");
    f.oceans[0].absorption={.2f,.1f,.05f};auto absorbed=render();auto position=renderer.readGBuffer(0);
    float distance=glm::length(glm::vec3(position[centre],position[centre+1],position[centre+2])-f.cameraPosition);
    for(int c=0;c<3;++c)require(std::abs(absorbed[centre+c]-clear[centre+c]*std::exp(-f.oceans[0].absorption[c]*distance))<.002,"Underwater fog failed independent Beer distance");
    f.oceans[0].underwaterCapture=false;auto noCapture=render();for(int c=0;c<3;++c)require(std::abs(noCapture[centre+c]-absorbed[centre+c])<.002,"Fog depended on a stale disabled capture");f.oceans[0].underwaterCapture=true;
    f.oceans[0].underwaterFog=false;auto off=render();for(int c=0;c<3;++c)require(std::abs(off[centre+c]-clear[centre+c])<.002,"Underwater fog switch left stale attenuation");
    f.oceans[0].underwaterFog=true;f.oceans[0].scattering=glm::vec3(.1f);f.lights[0].colorInner={1,1,1,0};auto scattered=render();auto captured=renderer.readWaterCapture(1);
    for(int c=0;c<3;++c){double sigma=f.oceans[0].absorption[c]+.1;
        double continuous=.1*.97963/(4.*3.141592653589793)*std::exp(-sigma)*(1.-std::exp(-2.*sigma*distance))/(2.*sigma);
        double expected=captured[centre+c]*std::exp(-sigma*distance)+continuous;
        require(std::abs(scattered[centre+c]-expected)<.002,"Underwater scattering differs from independent vertical slab integral");}
    for(int steps:{16,32}){f.oceans[0].underwaterVolumeSteps=steps;auto refined=render();
        for(int c=0;c<3;++c){double sigma=f.oceans[0].absorption[c]+.1;
            double expected=captured[centre+c]*std::exp(-sigma*distance)+.1*.97963/(4.*3.141592653589793)*std::exp(-sigma)*(1.-std::exp(-2.*sigma*distance))/(2.*sigma);
            require(std::abs(refined[centre+c]-expected)<.002,"Higher-quality underwater integration changed slab energy");}}
    f.oceans[0].underwaterVolumeSteps=4;
    f.shadows=true;f.shadowSettings.distance=20;f.lights[0].directionOuter={-1,-1,0,0};auto lit=render();auto litCapture=renderer.readWaterCapture(1);
    packets.push_back({waterQuad(d,.5f,.8f),glow,glm::translate(glm::mat4(1),glm::vec3(2,0,0))});auto shadowed=render();auto shadowCapture=renderer.readWaterCapture(1);packets.pop_back();
    const float T=std::exp(-(f.oceans[0].absorption.x+f.oceans[0].scattering.x)*distance);
    require(shadowed[centre]-shadowCapture[centre]*T<(lit[centre]-litCapture[centre]*T)*.9f,"Underwater eye scattering ignored an offscreen sun blocker");
    f.shadows=false;f.lights[0].directionOuter={0,-1,0,0};
    // Above-water emission, viewed from water: exact dielectric Fresnel and
    // one metre of eye-side attenuation, without counting the air leg.
    packets={{waterQuad(d,2,40),glow,glm::mat4(1)}};f.lights[0].colorInner=glm::vec4(0);f.oceans[0].absorption=f.oceans[0].scattering=glm::vec3(0);
    pointCamera({0,2,0});auto window=render();
    double f0=std::pow((1.333-1.)/(1.333+1.),2);
    for(int c=0;c<3;++c)require(std::abs(window[centre+c]-(1.-f0)*material.parameters.emissiveNormal[c])<.002,"Water-to-air window lost its above-water geometry");
    captured=renderer.readWaterCapture(1,true,true);require(captured[centre+3]>.5&&std::abs(captured[centre+1]-2)<.002,"Underwater view clipped away the air-side capture");
    auto waterOnly=renderer.readWaterCapture(1,true);require(waterOnly[centre+3]<.5,"Air-side geometry leaked into the underwater depth layer");
    f.oceans[0].absorption={.2f,.1f,.05f};auto foggedWindow=render();
    for(int c=0;c<3;++c)require(std::abs(foggedWindow[centre+c]-window[centre+c]*std::exp(-f.oceans[0].absorption[c]))<.002,"Interface eye path was missing or attenuated twice");
    f.oceans[0].absorption=glm::vec3(0);packets={{waterQuad(d,60,200),glow,glm::mat4(1)}};f.farPlane=100;pointCamera({0,60,0});auto farAir=render();
    for(int c=0;c<3;++c)require(std::abs(farAir[centre+c]-window[centre+c])<.002,"Water optical range incorrectly clipped the air-side geometry");
    // Environment LUTs deliberately exclude the solar disk. Direct air
    // transmission must include it without adding it to BRDF irradiance.
    packets.clear();f.sky=true;f.atmosphere.radii.y=.02f;f.lights[0].colorInner={1,1,1,0};pointCamera({0,2,0});
    auto solarWindow=render();require(solarWindow[centre]>10,"Refracted sky omitted the HDR solar disk");
    f.sky=false;f.atmosphere.radii.y=.005f;f.lights[0].colorInner=glm::vec4(0);
    // A near-critical bent ray reaches an object outside the original camera
    // frustum. The primary air layer is empty, so only the wide view can supply it.
    f.cameraPosition.y=-6;pointCamera({6*std::tan(glm::radians(47.f)),0,0});
    auto world=glm::inverse(f.viewProjection)*glm::vec4(2*(32.5f)/64-1,1-2*(32.5f)/64,1,1);
    auto waterRay=glm::normalize(glm::vec3(world)/world.w-f.cameraPosition),airRay=glm::refract(waterRay,glm::vec3(0,-1,0),1.333f);
    auto surfacePoint=f.cameraPosition+waterRay*(-f.cameraPosition.y/waterRay.y),airTarget=surfacePoint+airRay*(2/airRay.y);
    packets={{waterQuad(d,2,1.5f),glow,glm::translate(glm::mat4(1),glm::vec3(airTarget.x,0,airTarget.z))}};
    auto offscreenAir=render();require(offscreenAir[centre]>.1,"Wide air capture lost a refracted offscreen object");
    auto primaryAir=renderer.readWaterCapture(1,true,true);size_t primaryHits=0;for(size_t i=3;i<primaryAir.size();i+=4)primaryHits+=primaryAir[i]>.5;
    require(primaryHits==0,"Offscreen air regression did not place the target outside the primary frustum");
    f.oceans[0].underwaterWideRefraction=false;auto primaryOnly=render();require(primaryOnly[centre]<.001,"Wide air switch retained stale offscreen geometry");
    f.oceans[0].underwaterWideRefraction=true;require(render()[centre]>.1,"Wide air view did not restore after enabling");
    f.cameraPosition.y=-1;
    packets={{waterQuad(d,2,40),glow,glm::mat4(1)}};
    f.oceans[0].absorption=glm::vec3(0);pointCamera({std::tan(glm::radians(60.f)),0,0});auto tir=render();
    require(tir[centre]<.001&&tir[centre+1]<.001&&tir[centre+2]<.001,"Total internal reflection leaked the bright air-side background");
    pointCamera({std::tan(glm::radians(30.f)),0,0});auto belowCritical=render();require(belowCritical[centre]>.20,"Refraction below the critical angle was incorrectly rejected");
    // The straight camera ray to the air target intersects a submerged occluder,
    // but the physical bent ray does not. One nearest-depth layer loses this
    // target; the independent air layer must retain it.
    const float sinAir=1.333f*.5f,cosAir=std::sqrt(1-sinAir*sinAir);
    const float targetX=std::tan(glm::radians(30.f))+2*sinAir/cosAir;
    packets={{waterQuad(d,2,.12f),glow,glm::translate(glm::mat4(1),glm::vec3(targetX,0,0))},
             {waterQuad(d,-.5f,.04f),glow,glm::translate(glm::mat4(1),glm::vec3(targetX/6,0,0))}};
    auto separated=render();require(separated[centre]>.20,"Submerged foreground occluded the bent air-side transmission ray");
    f.oceans[0].underwaterCapture=false;auto straightCapture=render();
    require(straightCapture[centre]<.05,"Air-layer regression fixture did not expose straight-camera occlusion");f.oceans[0].underwaterCapture=true;
    packets={{waterQuad(d,2,40),glow,glm::mat4(1)}};
    // A real underwater reflector must replace the black fallback at TIR.
    packets.push_back({waterQuad(d,-2,40),glow,glm::mat4(1)});pointCamera({std::tan(glm::radians(60.f)),0,0});render();
    // Screen-space capture cannot see this floor while looking up; stable
    // bathymetry supplies offscreen underwater reflection instead.
    std::vector<float> bedHeights(17*17,-2);std::array<ImageRGBA8,5> maps;maps[0]={1,1,{200,180,140,255}};
    f.oceans[0].bathymetry=prepareWaterBathymetry(heightVirtualSource(17,17,bedHeights),materialVirtualSource(maps));
    f.oceans[0].bathymetryModel=glm::scale(glm::mat4(1),glm::vec3(32,1,32));f.lights[0].colorInner={1,1,1,0};auto bedReflection=render();
    require(bedReflection[centre]>.03,"TIR failed to show the offscreen bathymetry reflector");
    f.oceans[0].bathymetry.reset();f.lights[0].colorInner=glm::vec4(0);f.oceans[0].absorption=glm::vec3(.1f);
    f.oceans[0].waterMask=std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{0,0,0,255}});pointCamera({0,-2,0});auto dry=render();
    require(std::abs(dry[centre]-.25)<.002,"Dry water mask applied underwater fog");
    f.oceans[0].waterMask.reset();f.taa=true;
    for(float y:{-.1f,.1f,-.2f,.2f,-1.f}){f.cameraPosition.y=y;pointCamera({0,2,0});render();}
    renderer.resize(47,33);render();f.oceans[0].underwaterView=false;render();f.oceans[0].underwaterView=true;render();
    std::cout<<"Underwater view: independent Beer/slab energy, capture/fog switches, volume shadow, one eye leg, far air-side capture, Snell window/solar disk/offscreen wide air/toggle, separated air capture behind submerged occluder, 30/60-degree transmission/TIR, offscreen bed reflection, dry mask, surface crossing/TSAA/odd resize passed\n";
}
// Complete silhouettes need an independent geometric reference: a centre-pixel
// energy check cannot detect self-occlusion holes or duplicated box faces.
void validateWaterMarker(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory) {
    constexpr int width=320,height=180;
    struct Box {glm::vec3 centre,half,color;};
    const std::array<Box,3> boxes={Box{{-7.4f,.2f,-24.6f},{1,.45f,1},{.6f,0,0}},
        Box{{-7.4f,1.4f,-24.6f},{.45f,.75f,.45f},{0,.6f,0}},
        Box{{-7.4f,2.23f,-24.6f},{.65f,.08f,.65f},{0,0,.6f}}};
    FrameData f;f.cameraPosition={-8,-1.2f,-28};f.ambient=0;f.toneMapping=false;f.viewportWidth=width;f.viewportHeight=height;
    const float yaw=glm::radians(80.f),pitch=glm::radians(50.f);
    glm::vec3 front(std::cos(yaw)*std::cos(pitch),std::sin(pitch),std::sin(yaw)*std::cos(pitch));
    f.view=glm::lookAt(f.cameraPosition,f.cameraPosition+front,glm::vec3(0,1,0));glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    f.viewProjection=depth*glm::perspective(glm::radians(62.f),float(width)/height,.1f,100.f)*f.view;
    f.lights={{{0,0,0,0},{0,0,0,0},{0,-1,0,0}}};
    OceanSurfaceSettings s;s.spectrum.size=16;s.spectrum.length=16;s.spectrum.amplitude=0;s.surfaceLength=128;s.meshSize=33;
    s.seaLevel=0;s.detailWaves=false;s.robustRefraction=true;s.underwaterFog=false;s.absorption=s.scattering=glm::vec3(0);s.cameraGrid=false;f.oceans={s};
    std::vector<DrawPacket> packets;
    for(const auto& b:boxes){
        std::vector<MeshVertex> vertices;
        for(int i=0;i<8;++i)vertices.push_back({{float(i&1?1:-1),float(i&2?1:-1),float(i&4?1:-1)},{0,1,0},{0,0}});
        auto mesh=std::make_shared<GpuMesh>(d,vertices,std::vector<uint32_t>{0,2,3,0,3,1,4,5,7,4,7,6,0,4,6,0,6,2,1,3,7,1,7,5,0,1,5,0,5,4,2,6,7,2,7,3});
        MaterialDesc material;material.parameters.albedoAlpha={0,0,0,1};material.parameters.emissiveNormal=glm::vec4(b.color,0);
        packets.push_back({mesh,std::make_shared<GpuMaterial>(d,material),glm::scale(glm::translate(glm::mat4(1),b.centre),b.half)});
    }
    ForwardPbrRenderer renderer(d,directory,width,height,PbrPath::Scene);renderer.render(f,packets);auto gpu=renderer.readHDR();
    auto trace=[&](glm::vec3 origin,glm::vec3 ray,float& distance){
        glm::vec3 color(0);distance=1e6f;
        for(const auto& b:boxes){float low=0,high=1e6f;
            for(int axis=0;axis<3;++axis){
                if(std::abs(ray[axis])<1e-8f){if(std::abs(origin[axis]-b.centre[axis])>b.half[axis])high=-1;}
                else{float a=(b.centre[axis]-b.half[axis]-origin[axis])/ray[axis],z=(b.centre[axis]+b.half[axis]-origin[axis])/ray[axis];low=std::max(low,std::min(a,z));high=std::min(high,std::max(a,z));}
            }
            if(high>=low&&high>0&&low<distance){distance=low;color=b.color;}
        }return color;
    };
    std::vector<float> reference(width*height*4,0);const auto inverse=glm::inverse(f.viewProjection);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        auto world=inverse*glm::vec4(2*(x+.5f)/width-1,1-2*(y+.5f)/height,1,1);
        auto ray=glm::normalize(glm::vec3(world)/world.w-f.cameraPosition);float directT;auto result=trace(f.cameraPosition,ray,directT);
        float surfaceT=-f.cameraPosition.y/ray.y;
        if(ray.y>0&&surfaceT<directT){auto point=f.cameraPosition+ray*surfaceT;
            const double eta=1.333,cosine=ray.y,sin2=eta*eta*(1-cosine*cosine);double fresnel=1;glm::vec3 air(0);
            if(sin2<1){double ct=std::sqrt(1-sin2),rs=(eta*cosine-ct)/(eta*cosine+ct),rp=(cosine-eta*ct)/(cosine+eta*ct);fresnel=(rs*rs+rp*rp)/2;
                glm::vec3 refracted(eta*ray.x,ct,eta*ray.z);float t;air=trace(point+refracted*.001f,refracted,t);}
            glm::vec3 reflected(ray.x,-ray.y,ray.z);float t;auto water=trace(point+reflected*.001f,reflected,t);
            result=air*float(1-fresnel)+water*float(fresnel);
        }
        size_t i=4*(y*width+x);for(int c=0;c<3;++c)reference[i+c]=result[c];reference[i+3]=1;
    }
    size_t interior=0,mismatch=0;double error=0;
    for(int y=2;y<height-2;++y)for(int x=2;x<width-2;++x){size_t i=4*(y*width+x);float variation=0;
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)for(int c=0;c<3;++c)variation=std::max(variation,std::abs(reference[i+c]-reference[4*((y+dy)*width+x+dx)+c]));
        if(variation>.025f)continue;float difference=0;for(int c=0;c<3;++c)difference=std::max(difference,std::abs(reference[i+c]-gpu[i+c]));
        ++interior;error+=difference;if(difference>.04f)++mismatch;
    }
    require(interior>50000 && mismatch<double(interior)*.002 && error/interior<.001,"Water marker differs from independent Snell geometry");
    if(const char* output=std::getenv("SCENERENDERER_WATER_REFERENCE_OUTPUT")){
        std::filesystem::create_directories(output);
        auto save=[&](const char* name,const std::vector<float>& values){std::vector<uint8_t> pixels(values.size());for(size_t i=0;i<values.size();++i)pixels[i]=uint8_t(std::clamp(values[i],0.f,1.f)*255);auto path=std::filesystem::path(output)/name;require(stbi_write_png(path.string().c_str(),width,height,4,pixels.data(),width*4),"Cannot save marker reference");};
        save("marker-cpu.png",reference);save("marker-gpu.png",gpu);
        std::ofstream(std::filesystem::path(output)/"marker-reference.txt")<<"Interior pixels "<<interior<<", mismatch "<<mismatch<<", mean max-channel error "<<error/interior<<'\n';
    }
    std::cout<<"Water marker geometric reference: interior "<<interior<<", mismatches "<<mismatch<<", mean error "<<error/interior<<'\n';
}
}
void validateWaterSurface(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory){
    validateSunShafts(d,directory);validateCaustics(d,directory);validateWaterTransport();validateShore(d,directory);validateCoastEdges(d,directory);
    FrameData f;f.cameraPosition={0,2,0};f.ambient=0;f.toneMapping=false;
    glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    auto pointCamera=[&](){f.view=glm::lookAt(f.cameraPosition,glm::vec3(0,-2,0),glm::vec3(0,0,-1));f.viewProjection=depth*glm::perspective(glm::radians(20.f),1.f,.1f,40.f)*f.view;};pointCamera();
    f.lights={{{0,0,0,0},{0,0,0,0},{0,-1,0,0}}};
    OceanSurfaceSettings s;s.spectrum.size=16;s.spectrum.length=16;s.spectrum.amplitude=0;s.meshSize=33;s.seaLevel=0;s.detailWaves=false;s.cameraGrid=false;
    s.absorption=s.scattering=glm::vec3(0);s.specular=glm::vec3(0);s.fresnel=0;s.deepWaterDistance=40;s.anisotropy=0;
    MaterialDesc material;material.parameters.albedoAlpha={0,0,0,1};material.parameters.factors={0,1,1,0};material.parameters.emissiveNormal={.25f,.15f,.08f,0};
    auto floor=waterQuad(d,-2,12);auto glow=std::make_shared<GpuMaterial>(d,material);
    std::vector<DrawPacket> packets{{floor,glow,glm::mat4(1)}};f.oceans={s};
    ForwardPbrRenderer renderer(d,directory,64,64,PbrPath::Scene);
    auto render=[&](){renderer.render(f,packets);auto result=renderer.readHDR();for(float v:result)require(std::isfinite(v),"Water produced nonfinite radiance");return result;};
    const size_t centre=4*(32*64+32);auto clear=render();
    for(int c=0;c<3;++c)require(std::abs(clear[centre+c]-material.parameters.emissiveNormal[c])<.003,"Clear water lost the underwater background");
    f.oceans[0].opticalDebug=1;auto transmission=render();for(int c=0;c<3;++c)require(std::abs(transmission[centre+c]-1)<.002,"Zero-extinction water was not transparent");
    f.lights[0].colorInner={1,1,1,0};
    f.oceans[0].absorption={.2f,0,.05f};f.oceans[0].scattering={.1f,0,.15f};f.oceans[0].opticalDebug=2;auto lengths=render();float distance=lengths[centre]*40;
    require(std::abs(distance-2)<.02,"Flat water used the deep-water fallback instead of its floor");
    f.oceans[0].opticalDebug=1;transmission=render();glm::vec3 sigmaT=f.oceans[0].absorption+f.oceans[0].scattering;
    for(int c=0;c<3;++c)require(std::abs(transmission[centre+c]-std::exp(-sigmaT[c]*distance))<.002,"Water Beer transmittance differs from its analytic path");
    f.oceans[0].opticalDebug=0;auto volume=render();double maxError=0;
    // Flat water, isotropic phase and vertical sun: both light legs have length t.
    // This continuous integral is independent of the shader's four quadrature samples.
    for(int c=0;c<3;++c){double sigma=sigmaT[c],scatter=f.oceans[0].scattering[c];
        double integral=sigma>0?scatter*(1-std::exp(-2*sigma*distance))/(2*sigma):0;
        double floorSpecular=.04*.97963/(4*3.141592653589793)*std::exp(-2*sigma*distance);
        double expected=material.parameters.emissiveNormal[c]*std::exp(-sigma*distance)+floorSpecular+.97963/(4*3.141592653589793)*integral;
        maxError=std::max(maxError,std::abs(volume[centre+c]-expected));require(std::abs(volume[centre+c]-expected)<.003,"Single-scattering integration differs from flat-water analytic energy");}
    f.forwardShading=true;auto forward=render();for(int c=0;c<3;++c)require(std::abs(forward[centre+c]-volume[centre+c])<.002,"Forward/deferred water capture differs");f.forwardShading=false;
    auto captured=renderer.readWaterCapture(1,true);require(captured[centre+3]>.5&&std::abs(captured[centre+1]+2)<.001,"Underwater position capture missing");
    // A water-side color/depth layer must survive foreground geometry in the main G-buffer.
    packets.push_back({waterQuad(d,.5f,.4f),glow,glm::mat4(1)});render();auto opaque=renderer.readGBuffer(0);captured=renderer.readWaterCapture(1,true);size_t hidden=0;
    for(size_t i=0;i<captured.size();i+=4)if(opaque[i+3]>0&&opaque[i+1]>.4f&&captured[i+3]>.5&&captured[i+1]<-1.9f)++hidden;
    require(hidden>50,"Above-water occluder removed the independent underwater layer");packets.pop_back();
    // Offset sun blocker casts a shadow into the volume without obscuring
    // the camera's water pixel. Refraction is disabled to isolate the source.
    const auto unshadowedFrame=f;
    f.oceans[0].refraction=false;f.oceans[0].deepWaterDistance=2;f.shadows=true;f.shadowSettings.distance=20;
    f.lights[0].directionOuter={-1,-1,0,0};auto litVolume=render();
    packets.push_back({waterQuad(d,1,.9f),glow,glm::translate(glm::mat4(1),glm::vec3(2,0,0))});auto shadowedVolume=render();packets.pop_back();
    require(shadowedVolume[centre]<litVolume[centre]*.85f,"Water volume ignored an offscreen solar shadow caster");
    const float shadowRatio=shadowedVolume[centre]/std::max(litVolume[centre],1e-6f);f=unshadowedFrame;
    f.oceans[0].cameraGrid=true;f.oceans[0].surfaceLength=128;f.oceans[0].gridFocus=2;f.taa=true;render();f.cameraPosition.x=.2f;pointCamera();for(int i=0;i<4;++i)render();
    renderer.resize(48,32);render();require(renderer.readWaterCapture(1,true).size()==48*32*4,"Underwater capture did not follow resize");
    f.oceans[0].waterMask=std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{0,0,0,255}});render();
    f.oceans[0].waterMask.reset();f.taa=false;f.cameraPosition={0,2,0};pointCamera();renderer.resize(640,360);
    f.oceans[0].opticalDebug=3;renderer.render(f,{});auto empty=renderer.readHDR();
    require(empty[4*(180*640+320)]<.001,"Empty scene reused stale underwater capture");f.oceans[0].opticalDebug=0;
    // Native main render command buffers, same scene and optical coefficients.
    // Alternate old/new modes to reduce order-dependent GPU clock effects.
    ForwardPbrRenderer previousRenderer(d,directory,640,360,PbrPath::Scene);
    auto previousFrame=f;previousFrame.oceans[0].cameraGrid=previousFrame.oceans[0].underwaterCapture=previousFrame.oceans[0].volumeIntegration=false;
    f.oceans[0].cameraGrid=f.oceans[0].underwaterCapture=f.oceans[0].volumeIntegration=true;
    std::vector<double> oldTimes,newTimes;
    for(int i=0;i<20;++i){bool upgraded=(i&1)!=0;
        if(upgraded)renderer.render(f,packets);else previousRenderer.render(previousFrame,packets);
        d->waitIdle();auto timing=d->gpuTimingStats();
        if(i>=4&&timing.supported)(upgraded?newTimes:oldTimes).push_back(timing.milliseconds);
    }
    auto median=[](std::vector<double> v){if(v.empty())return 0.;std::sort(v.begin(),v.end());return (v[(v.size()-1)/2]+v[v.size()/2])*.5;};
    std::cout<<"Water benchmark 640x360 native render-buffer GPU median: previous "<<median(oldTimes)<<" ms, upgraded "<<median(newTimes)<<" ms; alternating modes, 2 warmup + 8 measured frames each (separately submitted FFT excluded)\n";
    std::cout<<"Water upgrade: clear/Beer/independent single-scattering energy (max error "<<maxError<<"), volume shadow ratio "<<shadowRatio<<", hidden underwater pixels "<<hidden<<", forward/deferred, focused grid/camera motion/TSAA, resize/mask passed\n";
    // New optical switches are tested independently of the original integral.
    renderer.resize(64,64);f=unshadowedFrame;f.oceans[0].robustRefraction=true;f.oceans[0].opticalDebug=2;
    auto dda=render();require(std::abs(dda[centre]*40-2)<.03,"Perspective DDA lost a planar floor");
    std::vector<float> bedHeights(33*33,-2);std::array<ImageRGBA8,5> maps;maps[0]={1,1,{180,150,100,255}};
    auto bathymetry=prepareWaterBathymetry(heightVirtualSource(33,33,bedHeights),materialVirtualSource(maps));
    auto detailedMaps=maps;detailedMaps[0]={128,128,std::vector<uint8_t>(128*128*4)};for(int y=0;y<128;++y)for(int x=0;x<128;++x){int i=4*(y*128+x);for(int c=0;c<3;++c)detailedMaps[0].pixels[i+c]=(x%4<2)?220:60;detailedMaps[0].pixels[i+3]=255;}
    auto fineBed=prepareWaterBathymetry(heightVirtualSource(33,33,bedHeights),materialVirtualSource(detailedMaps));require(fineBed->size==128,"Bathymetry downsampled higher-resolution bed colour to the height grid");for(int x=0;x<128;++x)require(std::abs(fineBed->heightColor[64*128+x].y-((x%4<2)?220:60)/255.f)<1e-5,"Bed colour lost fine texture detail");
    auto tinted=prepareWaterBathymetry(heightVirtualSource(33,33,bedHeights),materialVirtualSource(maps),{.5,.7,.9});
    for(const auto& h:tinted->heightColor)require(glm::length(glm::vec3(h.y,h.z,h.w)-glm::vec3(180,150,100)/255.f*glm::vec3(.5,.7,.9))<1e-5,"Bathymetry dropped the terrain albedo factor");
    for(const auto& h:bathymetry->heightColor)require(std::abs(h.x+2)<1e-6,"Stable bathymetry changed constant source height");
    f.oceans[0].bathymetry=bathymetry;f.oceans[0].bathymetryModel=glm::scale(glm::mat4(1),glm::vec3(16,1,16));f.oceans[0].opticalDebug=4;
    renderer.render(f,{});auto fallback=renderer.readHDR();require(fallback[centre+1]>.99&&fallback[centre]<.001,"Missing screen geometry did not use the world-space bed fallback");
    f.oceans[0].opticalDebug=2;renderer.render(f,{});fallback=renderer.readHDR();require(std::abs(fallback[centre]*40-2)<.03,"Terrain refraction fallback path length is wrong");
    f.oceans[0].opticalDebug=0;f.oceans[0].absorption=glm::vec3(.03);f.oceans[0].scattering=glm::vec3(.35);f.oceans[0].multipleScattering=false;auto single=render();
    f.oceans[0].multipleScattering=true;auto multiple=render();require(multiple[centre]>single[centre]+.0005,"Water multiple-scattering switch did not add its independent 2+ contribution");
    f.oceans[0].scattering=glm::vec3(0);auto pureAbs=render();f.oceans[0].multipleScattering=false;auto pureAbsOff=render();
    for(int c=0;c<3;++c)require(std::abs(pureAbs[centre+c]-pureAbsOff[centre+c])<.0002,"Multi-scattering switch changed pure absorption");
    f.oceans[0].waterMask=std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{0,0,0,255}});renderer.render(f,{});fallback=renderer.readHDR();require(fallback[centre]<.001,"Disabled shallow water ignored a black domain mask");
    f.oceans[0].shore.enabled=true;f.oceans[0].shore.resolution=32;f.oceans[0].shore.length=16;f.oceans[0].shore.swellHeight=0;f.oceans[0].opticalDebug=3;
    renderer.render(f,{});fallback=renderer.readHDR();require(fallback[centre]>.99,"Wet/dry shoreline did not replace a static dry mask inside its valid local domain");
    // Exercise live fluid history with TSAA, an overlapping camera scroll and
    // odd-size depth hierarchy reallocations, rather than just frozen switches.
    f.taa=true;f.oceans[0].waterMask.reset();f.oceans[0].opticalDebug=0;f.oceans[0].multipleScattering=true;f.oceans[0].shore.swellHeight=.15f;
    for(int frame=1;frame<=4;++frame){f.timeSeconds=frame*.1f;f.cameraPosition.x=frame*.25f;pointCamera();render();}
    renderer.resize(47,33);render();require(renderer.readWaterCapture(1,true).size()==47*33*4,"Active coastal capture failed odd-size resize");
    for(float value:renderer.readShoreWater(1))require(std::isfinite(value),"Live coastal history became nonfinite after camera scrolling");
    renderer.resize(64,64);f.taa=false;f.oceans[0].opticalDebug=3;f.oceans[0].waterMask=std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{0,0,0,255}});
    f.oceans[0].shore.enabled=false;renderer.render(f,{});fallback=renderer.readHDR();require(fallback[centre]<.001,"Disabling shore simulation left stale water coverage");
    std::cout<<"Water optional optics: DDA floor, independent world-space bathymetry, offscreen terrain hit/path, multi-scattering on/off and absorption limit, wet/dry mask override, live TSAA/camera scrolling/odd resize and stale-state rejection passed\n";
    validateUnderwater(d,directory);validateDiveVolume(d,directory);
    validateWaterMarker(d,directory);
}
}
