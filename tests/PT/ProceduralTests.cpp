#include "PT/CpuPathTracer.h"
#include "PT/ProceduralCapture.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
render::RenderWorldSnapshot camera(glm::vec3 p={0,1,0}){render::RenderWorldSnapshot s;s.frame.cameraPosition=p;glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;s.frame.viewProjection=depth*glm::perspective(glm::radians(50.f),1.f,.1f,100.f)*glm::lookAt(p,p+glm::vec3(0,-1,-.01f),glm::vec3(0,0,-1));return s;}
render::SnapshotTerrain terrain(){auto source=std::make_shared<render::TerrainPayload>();source->height.extent=64;source->height.heightField=true;source->height.formats={rhi::Format::RGBA32Float};
    source->height.readPage=[](uint32_t mip,uint32_t x,uint32_t y){check(mip==0,"Wrong height mip");std::vector<float> data(68*68*4);for(int j=0;j<68;++j)for(int i=0;i<68;++i)data[(j*68+i)*4]=.25f*std::clamp(int(x*64)+i-2,0,63)/63+.5f*std::clamp(int(y*64)+j-2,0,63)/63;render::VirtualTextureSource::Page page(1);page[0].resize(data.size()*4);std::memcpy(page[0].data(),data.data(),page[0].size());return page;};
    source->material.extent=64;source->material.formats.assign(5,rhi::Format::RGBA8UNorm);source->material.readPage=[](uint32_t,uint32_t,uint32_t){render::VirtualTextureSource::Page p(5,std::vector<uint8_t>(68*68*4,255));for(int j=0;j<68;++j)for(int i=0;i<68;++i){size_t at=(j*68+i)*4;p[0][at]=uint8_t(std::clamp(j-2,0,63)*4);p[1][at]=p[1][at+1]=128;p[2][at+2]=0;p[3][at+1]=128;}return p;};render::SnapshotTerrain t;t.source=source;t.parameters.factors={0,1,1,0};t.parameters.emissiveNormal.w=0;return t;}
pt::OceanSamples flat(){pt::OceanSamples s;s.size=8;s.displacement.resize(8*8*4);s.normal.resize(8*8*4);s.foam.resize(8*8*4);for(size_t i=0;i<s.normal.size();i+=4)s.normal[i+1]=1;return s;}
render::SnapshotDraw floor(float y){render::SnapshotDraw d;auto m=std::make_shared<render::MeshPayload>();m->vertices={{{-4,y,-4},{0,1,0},{0,0}},{{-4,y,4},{0,1,0},{0,1}},{{4,y,4},{0,1,0},{1,1}},{{4,y,-4},{0,1,0},{1,0}}};m->indices={0,1,2,0,2,3};d.mesh=m;d.parameters.albedoAlpha=glm::vec4(0,0,0,1);d.parameters.emissiveNormal={2,2,2,0};return d;}
void terrainTest(){auto t=terrain();pt::CaptureOptions options;options.terrainGrid=17;options.textureExtent=64;auto draw=pt::freezeTerrain(t,options);check(draw.mesh->vertices.size()==289&&draw.mesh->indices.size()==1536,"Full terrain mesh dimensions");auto N=glm::normalize(glm::vec3(-.125f,1,-.25f));for(auto v:draw.mesh->vertices)check(glm::length(v.normal-N)<1e-5f,"Height derivative normal");
    auto s=camera();s.draws.push_back(draw);pt::CpuScene scene(s);pt::Surface hit;check(scene.intersect({0,2,0},{0,-1,0},0,10,hit)&&std::abs(hit.position.y-.375f)<1e-5f,"PT terrain height mismatch");check(scene.proceduralCount(1)==1,"Terrain missing from scene metadata");check(glm::length(hit.albedo-glm::pow(glm::vec3(126/255.f,1,1),glm::vec3(2.2f)))<1e-4f,"Terrain material orientation mismatch");
    s.draws.clear();s.terrain=t;pt::CpuScene automatic(s);check(automatic.proceduralCount(1)==1&&automatic.triangles()>0,"CPU terrain auto capture failed");
    auto beach=std::make_shared<render::TerrainPayload>(*t.source);
    auto constant=[](glm::u8vec4 color){auto image=std::make_shared<render::ImageRGBA8>();image->width=2;image->height=4;for(int i=0;i<8;++i)for(int k=0;k<4;++k)image->pixels.push_back(color[k]);return image;};
    beach->shorelineImages={constant({200,160,100,255}),constant({128,128,255,255}),constant({255,180,0,255}),constant({255,255,255,255})};t.source=beach;t.extension.features.x=2;t.extension.shoreHeight={0,10,1,1};t.extension.shoreSurface={30,.7f,.95f,1};
    s.draws={pt::freezeTerrain(t,options)};s.terrain.reset();pt::CpuScene sand(s);check(sand.intersect({0,2,0},{0,-1,0},0,10,hit),"Beach surface missing");float wet=1-glm::smoothstep(-1.f,1.f,.375f);auto expected=glm::pow(glm::vec3(200,160,100)/255.f,glm::vec3(2.2f))*glm::mix(1.f,.45f,wet);check(glm::length(hit.albedo-expected)<.012f,"Shoreline material/height/wetness mismatch");check(hit.metallic==0&&std::abs(hit.roughness-glm::mix(180/255.f,90/255.f,wet))<.01,"Shoreline roughness capture mismatch");
    // Two points inside one global bake texel must retain distinct 30 m tiled sand detail.
    t.model=glm::scale(glm::mat4(1),glm::vec3(4000,1,4000));t.extension.shoreHeight={-2,10,1,1};
    auto detailed=std::make_shared<render::TerrainPayload>(*beach);auto striped=std::make_shared<render::ImageRGBA8>();striped->width=2;striped->height=4;
    striped->pixels={255,0,0,255,0,0,255,255,255,0,0,255,0,0,255,255,0,255,0,255,0,255,0,255,0,255,0,255,0,255,0,255};
    detailed->shorelineImages[0]=striped;t.source=detailed;auto detailDraw=pt::freezeTerrain(t,options);
    check(detailDraw.pathTracingShoreline[0]->height==2&&detailDraw.pathTracingShoreline[0]->pixels.size()==16,"Beach mip atlas was not restricted to level zero");
    s.draws={detailDraw};pt::CpuScene detailScene(s);pt::Surface red,blue,repeated;
    check(detailScene.intersect({7.5f,2,-7.5f},{0,-1,0},0,10,red)&&detailScene.intersect({22.5f,2,-7.5f},{0,-1,0},0,10,blue)&&detailScene.intersect({37.5f,2,-7.5f},{0,-1,0},0,10,repeated),"World-space sand test missed terrain");
    check(glm::length(red.albedo-glm::vec3(1,0,0))<.002f&&glm::length(blue.albedo-glm::vec3(0,0,1))<.002f&&glm::length(red.albedo-repeated.albedo)<.002f,"Sand detail was baked at landscape resolution instead of repeated at world scale");
    const auto packed=detailScene.exportData();check(packed.materials[0].shoreTextures.x!=UINT32_MAX&&packed.images[packed.materials[0].shoreTextures.x].y==2,"GPU shoreline textures missing");
    auto broken=std::make_shared<render::TerrainPayload>(*t.source);broken->height.readPage=[](uint32_t,uint32_t,uint32_t){return render::VirtualTextureSource::Page(1);};t.source=broken;bool rejected=false;try{pt::freezeTerrain(t,options);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Truncated terrain page accepted");}
void waterTest(){auto samples=flat();render::OceanSurfaceSettings settings;settings.meshSize=9;settings.spectrum.length=8;settings.detailWaves=false;settings.seaLevel=0;settings.absorption={.2f,.1f,.05f};settings.scattering=glm::vec3(0);auto draw=pt::freezeOcean(settings,samples,{});check(draw.mesh->vertices.size()==81&&draw.mesh->indices.size()==384,"FFT ocean topology");check(draw.pathTracingIor==1.333f&&draw.pathTracingKind==3,"Missing water optical metadata");
    auto tilted=samples;for(size_t i=0;i<tilted.normal.size();i+=4){tilted.normal[i]=-.8f;tilted.normal[i+1]=.6f;}auto grazing=camera();grazing.draws={pt::freezeOcean(settings,tilted,{})};pt::CpuScene grazingScene(grazing);pt::Surface grazingHit;auto incoming=glm::normalize(glm::vec3(-1,-1,0));check(grazingScene.intersect({1,1,0},incoming,0,10,grazingHit)&&glm::dot(grazingHit.normal,-incoming)>0,"Grazing shading normal faces away from incoming ray");
    auto s=camera();s.draws={draw,floor(-2)};pt::CpuScene scene(s);scene.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));pt::Surface hit;check(scene.intersect({0,1,0},{0,-1,0},0,10,hit)&&hit.water&&hit.frontFace,"Ocean interface orientation");
    bool bdptRejected=false;try{scene.validateBidirectional();}catch(const std::invalid_argument &){bdptRejected=true;}check(bdptRejected,"BDPT accepted unsupported water media");
    glm::vec3 mean(0);uint64_t rays=0;for(int i=0;i<20000;++i){pt::Random random(i+1);mean+=scene.trace({0,1,0},{0,-1,0},random,1,rays);}mean/=20000.f;float f=pt::dielectricFresnel(1,1,1.333f);auto expected=glm::vec3(f)+(1-f)/(1.333f*1.333f)*2.f*glm::exp(-settings.absorption*2.f);std::cout<<"Water Beer/Fresnel error "<<glm::length(mean-expected)<<'\n';check(glm::length(mean-expected)<.012f,"Water transmission/Beer energy mismatch");
    // Independent normal-incidence solar transmission: two Fresnel interfaces, two Beer segments.
    auto solarFloor=floor(-2);solarFloor.parameters.albedoAlpha=glm::vec4(1);solarFloor.parameters.emissiveNormal=glm::vec4(0);solarFloor.parameters.factors={0,1,1,0};
    auto solarSnapshot=camera();solarSnapshot.draws={draw,solarFloor};pt::CpuScene solar(solarSnapshot);solar.sunDirection={0,1,0};solar.sunIrradiance=glm::vec3(1);solar.sunRadius=.03f;
    glm::dvec3 solarMean(0);for(int i=0;i<30000;++i){pt::Random r(i+901);solarMean+=glm::dvec3(solar.trace({0,1,0},glm::normalize(glm::vec3(.2f,-1,0)),r,4,rays))/30000.;}
    const float cameraCos=1/std::sqrt(1.04f),waterCos=std::sqrt(1-(1-cameraCos*cameraCos)/(1.333f*1.333f)),cameraF=pt::dielectricFresnel(cameraCos,1,1.333f);
    // At roughness 1, GGX D=1/pi and Smith G(v)=2*v/(v+1).
    const float halfCos=std::sqrt((1+waterCos)/2),schlick=.04f+.96f*std::pow(1-halfCos,5.f);
    const float floorBsdf=((1-schlick)+schlick/(2*(1+waterCos)))/3.14159265358979323846f;
    auto solarExpected=glm::exp(-settings.absorption*(2.f/waterCos+2.f))*((1-f)*(1-cameraF)*floorBsdf/(1.333f*1.333f));
    std::cout<<"Refracted solar energy "<<solarMean.x<<" / "<<solarExpected.x<<'\n';check(glm::length(glm::vec3(solarMean)-solarExpected)/glm::length(solarExpected)<.04f,"Water solar proposal changed Fresnel/Beer energy");
    s.frame.cameraPosition={0,-1,0};pt::CpuScene underwater(s);check(glm::length(underwater.initialAbsorption({0,-1,0})-settings.absorption)<1e-6,"Underwater primary medium missing");pt::Random random(17);auto value=underwater.trace({0,-1,0},{0,-1,0},random,1,rays);check(glm::length(value-2.f*glm::exp(-settings.absorption))<.001f,"Underwater camera absorption mismatch");
    auto selective=draw;selective.pathTracingAbsorption={.2f,0,.05f};s.draws={selective};pt::CpuScene openWater(s);openWater.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));pt::Random sideways(11);auto escaped=openWater.trace({0,-1,0},{1,0,0},sideways,1,rays);check(glm::length(escaped-glm::vec3(0,1,0))<1e-6,"Infinite medium zero absorption channel limit");
    selective.pathTracingAbsorption.x=-1;bool invalid=false;try{s.draws={selective};pt::CpuScene bad(s);}catch(const std::invalid_argument &){invalid=true;}check(invalid,"Negative medium absorption accepted");
    settings.waterMask=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,{255,255,255,255,255,255,255,255,0,0,0,255,0,0,0,255}});auto masked=pt::freezeOcean(settings,samples,{});s.draws={masked};pt::CpuScene mask(s);check(mask.intersect({0,1,2},{0,-1,0},0,10,hit)&&!mask.intersect({0,1,-2},{0,-1,0},0,10,hit),"Shoreline mask orientation/alpha test");
    for(size_t i=0;i<samples.foam.size();i+=4)samples.foam[i]=1;settings.waterMask.reset();s=camera();s.draws={pt::freezeOcean(settings,samples,{})};pt::CpuScene foam(s);check(foam.intersect({0,1,0},{0,-1,0},0,10,hit)&&hit.foam>.99,"FFT foam missing");auto sample=pt::sampleBsdf(hit,{0,1,0},random);check(!sample.delta&&!sample.transmission&&sample.pdf>0,"Opaque foam mixture failed");check(std::abs(sample.pdf-pt::bsdfPdf(hit,{0,1,0},sample.direction))<1e-5,"Foam continuous PDF mismatch");
    s.frame.oceans={settings};bool rejected=false;try{pt::CpuScene unfrozen(s);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Unfrozen ocean was silently excluded");}
}
int main(){try{terrainTest();waterTest();std::cout<<"Procedural PT tests passed\n";return 0;}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
