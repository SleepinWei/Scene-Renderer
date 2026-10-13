#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "renderer/rhi/FeatureScenes.h"
#include "renderer/rhi/ProceduralSand.h"
#include "renderer/rhi/TreeGeometry.h"
#include "renderer/SceneImport.h"
#include "component/Model.h"
#include "component/GameObject.h"
#include "component/transform.h"
#include "component/Mesh_Filter.h"
#include "component/Mesh_Renderer.h"
#include "component/Lights.h"
#include "component/Atmosphere.h"
#include "component/Cloud.h"
#include "component/Ocean.h"
#include "component/TerrainComponent.h"
#include "component/Grass.h"
#include "object/Terrain.h"
#include "renderer/RenderScene.h"
#include "renderer/Material.h"
#include "renderer/Texture.h"
#include "renderer/RenderPass.h"
#include "renderer/TemporalAA.h"
#include "buffer/ImageTexture.h"
#include "object/SkyBox.h"
#include "system/RenderManager.h"
#include "system/InputManager.h"
#include "utils/Camera.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
std::shared_ptr<Texture> constantTexture(glm::vec3 color) {
    auto texture = std::make_shared<Texture>();
    std::vector<unsigned char> pixels(48);
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c)
            pixels[i * 3 + c] = static_cast<unsigned char>(255 * color[c]);
    texture->setPixels(4,4,3,std::move(pixels));
    return texture;
}
std::shared_ptr<Material> pbr(glm::vec3 color, float roughness = .6f, float metallic = 0) {
    auto material = std::make_shared<Material>();
    material->addTexture(constantTexture(color), "material.albedo");
    material->addTexture(constantTexture({.5f,.5f,1}), "material.normal");
    material->addTexture(constantTexture(glm::vec3(roughness)), "material.roughness");
    material->addTexture(constantTexture(glm::vec3(metallic)), "material.metallic");
    material->addTexture(constantTexture(glm::vec3(1)), "material.ao");
    material->addTexture(constantTexture(glm::vec3(0)), "material.height");
    return material;
}
std::shared_ptr<Mesh> quad(std::array<glm::vec3,4> positions, glm::vec3 normal) {
    std::vector<Vertex> vertices(4);
    const glm::vec2 uv[] = {{0,0},{1,0},{1,1},{0,1}};
    for (int i = 0; i < 4; ++i) {
        vertices[i] = Vertex{};
        vertices[i].Position = positions[i];
        vertices[i].Normal = normal;
        vertices[i].TexCoords = uv[i];
        vertices[i].Tangent = glm::normalize(positions[1] - positions[0]);
        vertices[i].Bitangent = glm::cross(normal, vertices[i].Tangent);
    }
    std::vector<unsigned> indices = {0,1,2,0,2,3};
    if (glm::dot(glm::cross(positions[1]-positions[0], positions[2]-positions[0]), normal) < 0)
        indices = {0,2,1,0,3,2};
    return std::make_shared<Mesh>(vertices, indices);
}
std::vector<std::shared_ptr<Mesh>> box() {
    return {
        quad({glm::vec3(-1,-1,1),{1,-1,1},{1,1,1},{-1,1,1}}, {0,0,1}),
        quad({glm::vec3(1,-1,-1),{-1,-1,-1},{-1,1,-1},{1,1,-1}}, {0,0,-1}),
        quad({glm::vec3(-1,-1,-1),{-1,-1,1},{-1,1,1},{-1,1,-1}}, {-1,0,0}),
        quad({glm::vec3(1,-1,1),{1,-1,-1},{1,1,-1},{1,1,1}}, {1,0,0}),
        quad({glm::vec3(-1,1,1),{1,1,1},{1,1,-1},{-1,1,-1}}, {0,1,0}),
        quad({glm::vec3(-1,-1,-1),{1,-1,-1},{1,-1,1},{-1,-1,1}}, {0,-1,0})
    };
}
void addMeshes(const std::shared_ptr<RenderScene>& target, const std::string& name,
               const std::vector<std::shared_ptr<Mesh>>& meshes, const std::shared_ptr<Material>& material,
               glm::vec3 position = glm::vec3(0), glm::vec3 scale = glm::vec3(1),
               glm::vec3 rotation = glm::vec3(0), ShaderType shader = ShaderType::PBR) {
    auto object = std::make_shared<GameObject>(); object->name = name;
    auto transform = std::make_shared<Transform>();
    transform->setPosition(position); transform->setScale(scale); transform->setRotation(rotation);
    object->addComponent(transform);
    auto filter = std::make_shared<MeshFilter>();
    for (const auto& mesh : meshes) { mesh->setMaterial(material); filter->addMesh(mesh); }
    object->addComponent(filter);
    auto renderer = std::make_shared<MeshRenderer>(); renderer->setShader(shader);
    object->addComponent(renderer); object->setDeferred(shader == ShaderType::PBR); target->addObject(object);
}
std::vector<std::shared_ptr<Mesh>> imported(const std::string& path, float height, bool generateUV, glm::mat4 transform = glm::mat4(1)) {
    AssimpLoader loader;
    auto meshes = loader.loadModel(path, true);
    if (meshes.empty()) throw std::runtime_error("Cannot import " + path + "; run python3 tools/fetch_classic_assets.py");
    for (const auto& mesh : meshes) {auto vertices=mesh->getVertices();for (auto& v : vertices) {
        v.Position = glm::vec3(transform * glm::vec4(v.Position, 1));
        v.Normal = glm::normalize(glm::mat3(transform) * v.Normal);
    }mesh->setGeometry(std::move(vertices),mesh->getIndices());}
    glm::vec3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
    for (const auto& mesh : meshes) for (const auto& v : mesh->getVertices()) {
        low = glm::min(low, v.Position); high = glm::max(high, v.Position);
    }
    float scale = height / (high.y - low.y);
    auto center = glm::vec3((low.x+high.x)/2, low.y, (low.z+high.z)/2);
    for (const auto& mesh : meshes) {auto vertices=mesh->getVertices();for (auto& v : vertices) {
        // PLY scans have no UVs. Planar UVs keep the derivative normal-map basis defined.
        if (generateUV) v.TexCoords = {v.Position.x * 12, v.Position.z * 12};
        v.Position = (v.Position - center) * scale;
        glm::vec3 axis = std::abs(v.Normal.y) < .95f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
        v.Tangent = glm::normalize(glm::cross(axis, v.Normal));
        v.Bitangent = glm::cross(v.Normal, v.Tangent);
    }mesh->setGeometry(std::move(vertices),mesh->getIndices());}
    return meshes;
}
void sun(const std::shared_ptr<RenderScene>& target, glm::vec3 color, glm::vec3 direction) {
    auto object = std::make_shared<GameObject>(); object->name = "Gallery sun";
    object->addComponent(std::make_shared<Transform>());
    auto light = std::make_shared<DirectionLight>(); light->setColor(color); light->setDirection(glm::normalize(direction));
    object->addComponent(light); target->addObject(object);
}
void point(const std::shared_ptr<RenderScene>& target, glm::vec3 color, glm::vec3 position) {
    auto object = std::make_shared<GameObject>(); object->name = "Gallery point light";
    auto transform = std::make_shared<Transform>(); transform->setPosition(position); object->addComponent(transform);
    auto light = std::make_shared<PointLight>(); light->setColor(color);
    object->addComponent(light); target->addObject(object);
}
void atmosphere(const std::shared_ptr<RenderScene>& target) {
    auto sky = std::make_shared<Sky>(); sky->addComponent(std::make_shared<Atmosphere>());
    sky->addComponent(std::make_shared<Cloud>());
    sky->skybox->setInitialized(false); sky->skybox->addTexture(std::make_shared<Texture>(), "skybox");
    sky->width = sky->height = 4;
    for (int i = 0; i < 6; ++i) { sky->data[i] = static_cast<unsigned char*>(std::malloc(48)); std::fill(sky->data[i], sky->data[i]+48, 16); }
    target->addSky(sky);
}
// Organic reef geometry and all grass blades are batched into two draws.
float diveBed(float x,float z) {
    float sand=-10.5f+.45f*std::sin(x*.12f+z*.07f)+.25f*std::sin(z*.23f);
    return sand+4.8f*std::exp(-((x+8)*(x+8)/22+(z-5)*(z-5)/65))
        +3.1f*std::exp(-((x-9)*(x-9)/25+(z-13)*(z-13)/80))
        +2.f*std::exp(-((x+12)*(x+12)/60+(z-32)*(z-32)/100));
}
std::shared_ptr<Mesh> diveReef() {
    std::vector<Vertex> vertices;std::vector<unsigned> indices;
    constexpr int rings=14,sectors=24;
    for(int rock=0;rock<34;++rock) {
        float k=float(rock),x=(rock%2?1.f:-1.f)*(4.8f+std::fmod(k*2.71f,10.f));
        float z=-4.f+std::fmod(k*5.31f,43.f),size=.65f+std::fmod(k*1.39f,2.1f);
        if(rock==0){x=-4.5f;z=0;size=2.1f;}
        if(rock==1){x=5.8f;z=7;size=2.7f;}
        auto offset=unsigned(vertices.size());
        for(int y=0;y<=rings;++y)for(int a=0;a<=sectors;++a) {
            float theta=float(y)/rings*3.14159265f,phi=float(a)/sectors*6.2831853f;
            glm::vec3 d(std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi));
            float radius=1.f+.13f*std::sin(phi*3+k)*std::sin(theta)*std::sin(theta)+.09f*std::sin(theta*5+phi*2+k)*std::sin(theta);
            Vertex v{};v.Position=glm::vec3(x,diveBed(x,z)+size*.25f,z)+d*glm::vec3(size,size*.78f,size*.82f)*radius;
            v.TexCoords={float(a)/sectors,float(y)/rings};vertices.push_back(v);
        }
        for(int y=0;y<rings;++y)for(int a=0;a<sectors;++a) {
            unsigned i=offset+y*(sectors+1)+a,j=i+sectors+1;
            if(y!=0)indices.insert(indices.end(),{i,i+1,j});
            if(y!=rings-1)indices.insert(indices.end(),{i+1,j+1,j});
        }
    }
    for(size_t i=0;i<indices.size();i+=3){auto& a=vertices[indices[i]];auto& b=vertices[indices[i+1]];auto& c=vertices[indices[i+2]];
        auto n=glm::cross(b.Position-a.Position,c.Position-a.Position);a.Normal+=n;b.Normal+=n;c.Normal+=n;}
    for(auto& v:vertices){v.Normal=glm::length(v.Normal)>1e-6f?glm::normalize(v.Normal):glm::vec3(0,1,0);
        auto axis=std::abs(v.Normal.y)<.95f?glm::vec3(0,1,0):glm::vec3(1,0,0);v.Tangent=glm::normalize(glm::cross(axis,v.Normal));v.Bitangent=glm::cross(v.Normal,v.Tangent);}
    return std::make_shared<Mesh>(vertices,indices);
}
std::shared_ptr<Mesh> diveGrass() {
    std::vector<Vertex> vertices;std::vector<unsigned> indices;
    for(int clump=0;clump<160;++clump) {
        float x=-16.f+std::fmod(float(clump)*7.137f,32.f),z=-8.f+std::fmod(float(clump)*3.719f,42.f);
        if(std::abs(x)<3.4f)continue; // Open sandy swimming channel.
        for(int blade=0;blade<9;++blade) {
            float a=float(clump+blade)*2.399963f,h=.35f+std::fmod(float(clump*9+blade)*.137f,.75f);
            glm::vec3 root(x+.28f*std::cos(a),0,z+.28f*std::sin(a));root.y=diveBed(root.x,root.z)+.02f;
            auto side=glm::vec3(std::cos(a),0,std::sin(a))*.032f;unsigned offset=unsigned(vertices.size());
            for(int segment=0;segment<=4;++segment){float t=float(segment)/4;glm::vec3 p=root+glm::vec3(.32f*t*t*std::sin(a),h*t,.21f*t*t);
                for(int edge=0;edge<2;++edge){Vertex v{};v.Position=p+side*(edge?1.f:-1.f)*(1.f-.85f*t);v.Normal=glm::normalize(glm::cross(side,glm::vec3(0,1,.3f)));v.Tangent=glm::normalize(side);v.Bitangent=glm::cross(v.Normal,v.Tangent);v.TexCoords={float(edge),t};vertices.push_back(v);}}
            for(int segment=0;segment<4;++segment){unsigned i=offset+segment*2;indices.insert(indices.end(),{i,i+1,i+2,i+1,i+3,i+2});}
        }
    }
    return std::make_shared<Mesh>(vertices,indices);
}
void floor(const std::shared_ptr<RenderScene>& target) {
    addMeshes(target, "Gallery floor", {quad({glm::vec3(-30,0,30),{30,0,30},{30,0,-30},{-30,0,-30}}, {0,1,0})}, pbr({.32f,.36f,.41f}, .75f));
}
}
std::shared_ptr<RenderScene> render::makeClassicScene(const std::string& name) {
    if(name=="vegetation-lake"||name=="wave-particles-lake"){
        auto scene=makeClassicScene("nature-coast");
        auto vegetation=std::make_shared<Grass>();VegetationSettings plants;plants.species=meadowSpecies();plants.waterLevel=0;plants.shoreMargin=.3f;plants.heightScale=1;plants.widthScale=1;
        // Clear shoreline excludes grasses from wet sand; reeds stay on dry banks.
        scene->terrain()->addComponent(vegetation);vegetation->setSettings(plants);
        scene->mainCamera()->setPosition({-9,4.5f,-18});scene->mainCamera()->setAngles(76,-6);scene->mainCamera()->setExposure(1.8f);
        auto ocean=scene->terrain()->getComponent<Ocean>();ocean->updateSettings([&](auto& s){s.fft_size=256;s.MeshSize=257;s.MeshLength=4096;s.SpectrumLength=128;s.A=.000005f;s.detailStrength=.3f;s.shore.resolution=128;s.shore.length=64;s.shore.swellHeight=.08f;s.surfaceModel.physical=true;s.shortWaveRipples=true;s.rippleRmsHeight=.008f;
            s.waveParticles.enabled=true;s.waveParticles.length=64;s.waveParticles.resolution=256;s.waveParticles.speed=2.4f;s.waveParticles.damping=.08f;
            s.waveParticles.impulses={{1,6.4,{-5,-5},{1,0},1.2f,2.2f,6.2831853f}};
            s.waveParticles.emitters={{{-14,-9},{1.7f,.25f},0,30,.15f,.7f,1.8f}};
            if(name=="wave-particles-lake"){s.shore.enabled=false;s.A=0;s.detailWaves=false;s.absorption={.35f,.12f,.06f};s.scattering={.025f,.05f,.08f};s.waveParticles.impulses={{1,7,{-5,-5},{1,0},1.8f,1.4f,6.2831853f}};}
        });
        if(name=="wave-particles-lake"){
            auto ground=scene->terrain()->getComponent<TerrainComponent>();uint32_t w=ground->getHeightWidth(),h=ground->getHeightHeight();
            std::vector<float> heights(ground->getHeightData(),ground->getHeightData()+size_t(w)*h);for(auto& y:heights)if(y<0)y*=5;ground->setHeightData(w,h,std::move(heights));
            scene->mainCamera()->setPosition({-5,10,-15});scene->mainCamera()->setAngles(90,-40);for(auto light:scene->directionLights())light->setDirection({0,-.8f,-.6f});}
        RenderManager::GetInstance()->setting.shadowSettings.distance=180;
        return scene;
    }
    if(name=="coastal-seabed"){auto scene=makeClassicScene("coastal-underwater");scene->mainCamera()->setAngles(80,-35);return scene;}
    if(name=="ocean-sea-state"){
        auto scene=makeOceanScene(false);
        scene->mainCamera()->setExposure(1.4f);
        if(!scene->directionLights().empty())scene->directionLights()[0]->setDirection({0,-.42261826f,.90630779f});
        auto ocean=scene->terrain()->getComponent<Ocean>();
        ocean->updateSettings([](auto& s){s.seaState.enabled=true;s.surfaceModel.physical=true;s.MeshLength=2048;s.SpectrumLength=256;s.HeightScale=1;s.Lambda=.8f;s.shortWaveRipples=true;s.BubblesThreshold=.65f;s.BubblesScale=1.5f;});
        return scene;
    }
    if(name=="ocean" || name=="ocean-clear")return makeOceanScene(name=="ocean-clear");
    auto target = std::make_shared<RenderScene>();
    auto manager = RenderManager::GetInstance();
    manager->setting.enableShadow = true; manager->setting.enableSSAO = true; manager->setting.enableRSM = false;
    // Shadow attachments belong to the lights in each scene.


    if(name=="tree-single"||name=="tree-bare"||name=="tree-forest"||name=="tree-edge") {
        target->setCamera(std::make_shared<Camera>());
        atmosphere(target);sun(target,{4.8f,4.5f,3.8f},{-.65f,-.5f,-.4f});
        manager->setting.shadowSettings.distance=180;manager->setting.shadowSettings.depthBias=.0008f;
        target->mainCamera()->setPosition({-8,3.7f,-13});target->mainCamera()->setAngles(58,1);target->mainCamera()->setZoom(48);target->mainCamera()->setExposure(1.5f);
        if(name=="tree-forest"||name=="tree-edge"){
            auto terrain=std::make_shared<Terrain>();auto ground=std::make_shared<TerrainComponent>();constexpr uint32_t n=129;std::vector<float> heights(n*n);
            for(uint32_t y=0;y<n;y++)for(uint32_t x=0;x<n;x++){float wx=(float(x)/(n-1)-.5f)*128,wz=(float(y)/(n-1)-.5f)*128;heights[y*n+x]=.5f*std::sin(wx*.06f)*std::sin(wz*.08f);}
            ground->setHeightData(n,n,std::move(heights));ground->updateSettings([&](auto& t){t.model=glm::scale(glm::mat4(1),glm::vec3(64,1,64));t.material=pbr({.25f,.32f,.15f},.94f);t.maxLeaves=8192;});terrain->addComponent(ground);
            auto grass=std::make_shared<Grass>();auto defaults=meadowSpecies();VegetationSettings plants;plants.species={defaults[0],defaults[5],defaults[6],defaults[7]};plants.species[0].capacity=16384;plants.species[0].spacing=.3f;plants.species[0].farSpacing=1;plants.species[0].denseRadius=6;plants.species[0].sparseRadius=50;
            plants.species[1].spacing=6.8f;plants.species[1].density=.88f;plants.species[1].height=9;plants.species[1].width=10;plants.species[1].lodDistance=22;plants.species[1].lodTransition=8;
            plants.species[2].spacing=9;plants.species[2].density=.35f;plants.species[2].lodDistance=24;plants.species[2].lodTransition=8;plants.waterLevel=-100;grass->setSettings(plants);terrain->addComponent(grass);target->addTerrain(terrain);
            target->mainCamera()->setPosition({-7,3,-29});target->mainCamera()->setAngles(82,4);target->mainCamera()->setZoom(62);
            if(name=="tree-edge"){target->mainCamera()->setPosition({-7,5,-72});target->mainCamera()->setAngles(82,0);target->mainCamera()->setExposure(1.7f);}
        }else{
            auto tree=render::treeGeometry(PlantType::Broadleaf,0,71);
            auto part=[&](std::shared_ptr<const render::MeshPayload> payload,bool foliage){
                std::vector<Vertex> vertices;for(const auto& source:payload->vertices){Vertex v{};v.Position=source.position;v.Normal=source.normal;v.TexCoords=source.uv;vertices.push_back(v);}
                auto desc=render::treeMaterial(PlantType::Broadleaf,foliage);auto mat=pbr(glm::vec3(1),1);const char* slots[]={"material.albedo","material.normal","material.metallic","material.roughness","material.ao"};
                for(int i=0;i<5;i++)if(desc.sharedImages[i]){auto image=desc.sharedImages[i];auto texture=std::make_shared<Texture>();texture->setPixels(image->width,image->height,4,image->pixels);mat->addTexture(texture,slots[i]);}
                if(foliage){auto image=*desc.sharedSpecial;for(size_t i=0;i<image.pixels.size();i+=4)image.pixels[i]=image.pixels[i+3];auto texture=std::make_shared<Texture>();texture->setPixels(image.width,image.height,4,std::move(image.pixels));mat->addTexture(texture,"material.thickness");}
                auto props=mat->properties();props.alphaCutoff=desc.parameters.factors.w;props.twoSided=foliage;props.normalStrength=desc.parameters.emissiveNormal.w;props.foliageTransmission=foliage?.55f:0;mat->setProperties(props);
                auto animated=std::make_shared<Mesh>(vertices,payload->indices);animated->setTreeWind(payload->treeWind);
                addMeshes(target,foliage?"Tree leaves":"Tree branches",{animated},mat,{0,0,0},{10,9,10});
            };part(tree.bark,false);if(name!="tree-bare")part(tree.foliage,true);
            addMeshes(target,"Tree meadow floor",{quad({glm::vec3(-250,0,250),{250,0,250},{250,0,-250},{-250,0,-250}},{0,1,0})},pbr({.28f,.36f,.17f},.9f));
        }
    } else if(name=="underwater-dive") {
        auto terrain=std::make_shared<Terrain>();auto component=std::make_shared<TerrainComponent>();
        constexpr int n=513,texSize=2048;std::vector<float> heights(n*n);
        for(int y=0;y<n;++y)for(int x=0;x<n;++x){float wx=(float(x)/(n-1)-.5f)*128,wz=(float(y)/(n-1)-.5f)*128;
            heights[y*n+x]=diveBed(wx,wz)+.018f*std::sin(wx*14.f+std::sin(wz*1.1f));}
        auto bed=pbr(glm::vec3(1),.87f);auto texture=std::make_shared<Texture>();std::vector<uint8_t> pixels(texSize*texSize*3);
        for(int y=0;y<texSize;++y)for(int x=0;x<texSize;++x){float wx=(float(x)/(texSize-1)-.5f)*128,wz=(.5f-float(y)/(texSize-1))*128;
            float reef=glm::smoothstep(-9.4f,-7.5f,diveBed(wx,wz));
            float ripple=.5f+.5f*std::sin(wx*14.f+std::sin(wz*1.1f));
            uint32_t hash=uint32_t(x)*1664525u+uint32_t(y)*1013904223u;hash^=hash>>13;
            float noise=float(hash&255)/255.f,patch=.5f+.5f*std::sin(wx*2.3f)*std::sin(wz*3.1f);
            auto color=glm::mix(glm::vec3(.72f,.68f,.50f)*(.70f+.22f*ripple+.08f*noise),glm::vec3(.29f,.35f,.28f)*(.60f+.30f*patch+.10f*noise),reef);
            for(int c=0;c<3;++c)pixels[(y*texSize+x)*3+c]=uint8_t(color[c]*255);}
        texture->setPixels(texSize,texSize,3,std::move(pixels));bed->addTexture(texture,"material.albedo");component->setHeightData(n,n,std::move(heights));
        component->updateSettings([&](auto& v){v.model=glm::scale(glm::mat4(1),glm::vec3(64,1,64));v.material=bed;v.maxLeaves=8192;});terrain->addComponent(component);
        auto ocean=std::make_shared<Ocean>();ocean->updateSettings([](auto& v){v.fft_size=512;v.MeshSize=513;v.MeshLength=4096;v.SpectrumLength=128;v.seaLevel=0;
            v.A=.00008f;v.HeightScale=.65f;v.WindScale=10;v.Lambda=.25f;v.BubblesScale=.25f;v.detailStrength=.65f;v.shortWaveRipples=true;
            v.bedCaustics=true;v.robustRefraction=true;v.multipleScattering=true;
            v.absorption={.13f,.032f,.018f};v.scattering={.012f,.035f,.048f};v.scatteringAnisotropy=.55f;v.subsurfaceStrength=1.f;v.deepWaterDistance=90;
            v.underwaterSunShafts=true;v.sunShaftStrength=1.8f;v.underwaterParticles=true;v.particleDensity=.45f;v.underwaterVolumeSteps=32;v.shore.enabled=false;});terrain->addComponent(ocean);target->addTerrain(terrain);
        auto stone=pbr(glm::vec3(1),.92f);auto rockTexture=std::make_shared<Texture>();std::vector<uint8_t> rockPixels(256*256*3);
        for(int y=0;y<256;++y)for(int x=0;x<256;++x){float shade=.6f+.17f*std::sin(x*.19f+std::sin(y*.23f))+.13f*std::sin(x*.053f)*std::sin(y*.07f);auto c=glm::mix(glm::vec3(.35f,.39f,.30f),glm::vec3(.48f,.40f,.31f),.5f+.5f*std::sin(x*.11f+y*.07f))*shade;for(int k=0;k<3;++k)rockPixels[(y*256+x)*3+k]=uint8_t(c[k]*255);}
        rockTexture->setPixels(256,256,3,std::move(rockPixels));stone->addTexture(rockTexture,"material.albedo");addMeshes(target,"Dive reef outcrops",{diveReef()},stone);
        // A visible air-side reference for the diving scene's Snell window.
        const glm::vec3 buoy(0,0,-5);
        addMeshes(target,"Dive surface orange float",box(),pbr({1,.48f,.025f},.4f),buoy+glm::vec3(0,.12f,0),{1.2f,.28f,1.2f});
        addMeshes(target,"Dive surface marker mast",box(),pbr({1,.82f,.12f},.35f),buoy+glm::vec3(0,1.6f,0),{.18f,1.5f,.18f});
        addMeshes(target,"Dive surface blue cap",box(),pbr({.025f,.28f,1},.35f),buoy+glm::vec3(0,3.1f,0),{.55f,.1f,.55f});
        auto grass=pbr({.16f,.28f,.10f},.8f);grass->setTwoSided(true);addMeshes(target,"Dive seagrass meadows",{diveGrass()},grass);
        target->setCamera(std::make_shared<Camera>(glm::vec3(0,-6,-12),glm::vec3(0,1,0),90,-8));target->mainCamera()->setClipPlanes(.08f,600);target->mainCamera()->setZoom(68);target->mainCamera()->setExposure(1.35f);
        atmosphere(target);sun(target,glm::vec3(5),{-.35f,-.85f,-.35f});manager->setting.enableSSAO=false;manager->setting.shadowSettings.distance=90;
    } else if(name=="coastal-beach"||name=="coastal-water"||name=="coastal-underwater"||name=="nature-coast") {
        // A precise authored coast: source spacing 0.25 m, no downloaded asset.
        auto terrain=std::make_shared<Terrain>();auto component=std::make_shared<TerrainComponent>();
        constexpr uint32_t n=513;std::vector<float> heights(size_t(n)*n);
        for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x){float wx=(float(x)/(n-1)-.5f)*128,wz=(float(y)/(n-1)-.5f)*128;
            float shoreline=2.5f*std::sin(wx*.085f),beach=.075f*(wz-shoreline);
            float reef=.9f*std::exp(-((wx-12)*(wx-12)+(wz+8)*(wz+8))*.035f);
            float bedDetail=(.012f*std::sin(wx*.7f+.7f*std::sin(wz*.2f))+.006f*std::sin(wz*.8f+wx*.3f))*(1.f-glm::smoothstep(-.5f,.5f,beach));
            heights[size_t(y)*n+x]=beach+reef+bedDetail;
        }
        auto sand=pbr(glm::vec3(1),.85f,0);
        constexpr int grainSize=2048;
        auto grain=std::make_shared<Texture>(),sandNormals=std::make_shared<Texture>();std::vector<uint8_t> pixels(grainSize*grainSize*3),normalPixels(pixels.size());
        render::ProceduralSand sandField;
        for(int y=0;y<grainSize;++y)for(int x=0;x<grainSize;++x){
            float wx=(float(x)/(grainSize-1)-.5f)*128,wz=(.5f-float(y)/(grainSize-1))*128;
            auto sample=sandField.sample({wx,wz});auto color=sample.albedo;
            if(name=="nature-coast"){
                // Habitat colour is camera independent and survives geometry thinning.
                float elevation=.075f*(wz-2.5f*std::sin(wx*.085f));
                float cover=glm::smoothstep(.3f,.85f,elevation);
                float shade=sample.albedo.x/.76f;
                color=glm::mix(color,glm::vec3(.24f,.34f,.13f)*shade,cover);
                sample.gradient*=1-cover;
            }
            auto normal=glm::normalize(glm::vec3(-sample.gradient.x,sample.gradient.y,1));
            for(int c=0;c<3;++c){pixels[(y*grainSize+x)*3+c]=uint8_t(glm::clamp(255*color[c],0.f,255.f));normalPixels[(y*grainSize+x)*3+c]=uint8_t(255*(normal[c]*.5f+.5f));}
        }
        grain->setPixels(grainSize,grainSize,3,std::move(pixels));sand->addTexture(grain,"material.albedo");sandNormals->setPixels(grainSize,grainSize,3,std::move(normalPixels));sand->addTexture(sandNormals,"material.normal");
        component->setHeightData(n,n,std::move(heights));
        component->updateSettings([&](auto& v){v.model=glm::scale(glm::mat4(1),glm::vec3(64,1,64));v.material=sand;v.maxLeaves=8192;});
        terrain->addComponent(component);auto ocean=std::make_shared<Ocean>();
        ocean->updateSettings([&](auto& v){v.fft_size=512;v.MeshSize=513;v.MeshLength=4096;v.SpectrumLength=128;v.seaLevel=0;
            v.A=.00008f;v.HeightScale=.6f;v.WindScale=12;v.Lambda=.25f;v.BubblesScale=.3f;v.detailStrength=.35f;
            v.absorption={.08f,.025f,.012f};v.scattering={.01f,.02f,.025f};v.refractionStrength=1;
            v.robustRefraction=v.multipleScattering=true;v.shore.enabled=true;v.shore.swellHeight=.45f;v.shore.swellPeriod=5.5f;v.shore.swellDirection={.15f,1};v.shore.foamStrength=1;v.shore.foamLifetime=4;
        });terrain->addComponent(ocean);target->addTerrain(terrain);
        target->setCamera(std::make_shared<Camera>(glm::vec3(-12,6,23),glm::vec3(0,1,0),-76,-18));
        if(name=="coastal-water")target->setCamera(std::make_shared<Camera>(glm::vec3(-8,8,-28),glm::vec3(0,1,0),80,-30));
        if(name=="coastal-underwater") {
            ocean->updateSettings([](auto& v){v.shortWaveRipples=true;v.bedCaustics=true;v.detailStrength=1;});
            // A partly submerged marker gives the interface a recognizable
            // silhouette and straight colour boundaries to refract.
            const glm::vec3 marker(-7.4f,0,-24.6f);
            auto checked=pbr(glm::vec3(1),.45f);
            auto texture=std::make_shared<Texture>();
            std::vector<uint8_t> checks(128*128*3);
            for(int y=0;y<128;++y)for(int x=0;x<128;++x) {
                const glm::vec3 color=((x/32+y/32)&1)?glm::vec3(.95f,.95f,.88f):glm::vec3(.9f,.09f,.035f);
                for(int c=0;c<3;++c)checks[(y*128+x)*3+c]=uint8_t(color[c]*255);
            }
            texture->setPixels(128,128,3,std::move(checks));checked->addTexture(texture,"material.albedo");
            addMeshes(target,"Water demo checker float",box(),checked,marker+glm::vec3(0,.2f,0),{1,.45f,1});
            addMeshes(target,"Water demo orange marker",box(),pbr({1,.42f,.025f},.35f),marker+glm::vec3(0,1.4f,0),{.45f,.75f,.45f});
            addMeshes(target,"Water demo blue cap",box(),pbr({.025f,.32f,.95f},.3f),marker+glm::vec3(0,2.23f,0),{.65f,.08f,.65f});
            target->setCamera(std::make_shared<Camera>(glm::vec3(-8,-1.2f,-28),glm::vec3(0,1,0),80,50));
        }
        target->mainCamera()->setClipPlanes(.1f,6000);target->mainCamera()->setZoom(62);
            if(name=="tree-edge"){target->mainCamera()->setPosition({-7,5,-72});target->mainCamera()->setAngles(82,0);target->mainCamera()->setExposure(1.7f);}target->mainCamera()->setExposure(1.2f);
        atmosphere(target);sun(target,glm::vec3(3),{-.5f,-.6f,name=="coastal-underwater"?.4f:-.4f});manager->setting.enableSSAO=false;
    } else if (name == "mountain-lake" || name == "mountain-lake-ground" || name == "mountain-lake-beach") {
        std::ifstream input("samples/assets/terrain/mountain-lake/scene.json");
        if (!input) throw std::runtime_error("Mountain Lake is missing; download the official archives and run python3 tools/prepare_mountain_lake.py (see docs/mountain-lake.md)");
        nlohmann::json config; input >> config;
        auto terrain = std::make_shared<Terrain>();
        auto component = std::make_shared<TerrainComponent>();
        component->loadFromJson(config);
        const auto scale = config.at("modelScale").get<std::array<float,3>>();
        auto material = pbr(glm::vec3(1));
        material->setMetallicFactor(0);
        material->setRoughnessFactor(.9f);
        component->updateSettings([&](auto& value) {
            value.model = glm::scale(glm::mat4(1),glm::vec3(scale[0],scale[1],scale[2]));
            value.material = material;
        });
        terrain->addComponent(component);
        auto lake = std::make_shared<Ocean>();
        lake->updateSettings([&](auto& value) {
            value.FFTPow = 9; value.fft_size = 512; value.MeshSize = 1025;
            value.SpectrumLength = 512;
            value.MeshLength = scale[0]*2;
            value.seaLevel = config.at("seaLevel").get<float>();
            value.waterMaskPath = "samples/assets/masks/mountain-lake-water-mask.png";
            value.WindScale = 8; value.A = .0001f; value.HeightScale = .15f;
            value.Lambda = .15f; value.detailStrength = .5f;
            value.BubblesScale = 0;
            value.refractionStrength = .35f; value.deepWaterDistance = 40;
            value.absorption = {.08f,.025f,.012f};
            value.scattering = {.01f,.02f,.025f};
            value.outer_OceanColorShallow = {.18f,.65f,.7f};
            value.outer_OceanColorDeep = {.025f,.15f,.25f};
        });
        terrain->addComponent(lake);
        auto grass = std::make_shared<Grass>();
        grass->updateSettings([&](auto& value){
            value.capacity=65536;value.maxLod=3;value.samplesPerCell=8;value.distance=180;value.fadeStart=120;
            value.nearSpacing=.15f;value.farSpacing=3.5f;value.denseRadius=10;value.sparseRadius=70;
            value.density=.95f;value.heightScale=2.8f;value.widthScale=2.5f;
            value.minimumNormalY=.8f;value.waterLevel=lake->settings().seaLevel;value.shoreMargin=1.5f;
            value.maximumAltitude=1200;value.waterMaskPath=lake->settings().waterMaskPath;
        });
        terrain->addComponent(grass); target->addTerrain(terrain);
        target->setCamera(std::make_shared<Camera>(glm::vec3(-2600,800,2600),glm::vec3(0,1,0),-45,3));
        if(name=="mountain-lake-ground")
            target->setCamera(std::make_shared<Camera>(glm::vec3(-1050,464,2000),glm::vec3(0,1,0),-45,-4));
        if(name=="mountain-lake-beach")
            target->setCamera(std::make_shared<Camera>(glm::vec3(-1050,439,1750),glm::vec3(0,1,0),-60,-9));
        target->mainCamera()->setClipPlanes(.1f,16000);
        target->mainCamera()->setMovementSpeed(150);
        target->mainCamera()->setZoom(62);
            if(name=="tree-edge"){target->mainCamera()->setPosition({-7,5,-72});target->mainCamera()->setAngles(82,0);target->mainCamera()->setExposure(1.7f);} target->mainCamera()->setExposure(1.4f);
        atmosphere(target); sun(target,glm::vec3(3),{-.5f,-1,-.4f});
        manager->setting.enableSSAO = true;
        manager->setting.shadowSettings.distance=8000;
        manager->setting.shadowSettings.depthBias=.0005f;
    } else if (name == "shadow-test") {
        manager->setting.shadowSettings=render::ShadowSettings{};
        target->setCamera(std::make_shared<Camera>(glm::vec3(12,10,30),glm::vec3(0,1,0),-102,-12));
        target->mainCamera()->setClipPlanes(.1f,500);
        target->mainCamera()->setZoom(70);target->mainCamera()->setExposure(1);
        addMeshes(target,"Shadow receiver",{quad({glm::vec3(-120,0,40),{120,0,40},{120,0,-350},{-120,0,-350}},{0,1,0})},pbr({.65f,.68f,.72f},.85f));
        for(int i=0;i<3;++i) {
            float h=4.f+i*6.f;
            addMeshes(target,"Near shadow pillar "+std::to_string(i),box(),pbr({.55f,.28f,.12f},.7f),{float(i-1)*8,h/2,0},{1,h/2,1});
        }
        for(int i=0;i<7;++i)
            addMeshes(target,"Distant shadow pillar "+std::to_string(i),box(),pbr({.24f,.40f,.55f},.7f),{float(i%2?6:-6),3,-25.f-i*35},{1.5f,3,1.5f});
        sun(target,glm::vec3(3),{.4f,-1,.6f});
        manager->setting.enableSSAO=false;
    } else if (name == "terrain") {
        target->setCamera(std::make_shared<Camera>(glm::vec3(0,10,32),glm::vec3(0,1,0),-90,-12));
        target->mainCamera()->setZoom(60);target->mainCamera()->setExposure(1);
        auto terrain=std::make_shared<Terrain>();auto component=std::make_shared<TerrainComponent>();std::vector<float> heights(1024*1024);
        auto elevation=[](float x,float z){float h=.28f*std::exp(-((x+.35f)*(x+.35f)*5+(z+.25f)*(z+.25f)*3))+.5f*std::exp(-((x-.45f)*(x-.45f)*9+(z+.4f)*(z+.4f)*4));return h+.035f*std::sin(x*18+z*13)*std::cos(z*16)*h;};
        for(uint32_t y=0;y<1024;y++)for(uint32_t x=0;x<1024;x++)heights[y*1024+x]=elevation(x/1023.f*2-1,y/1023.f*2-1);
        component->setHeightData(1024,1024,std::move(heights));
        component->updateSettings([&](auto& value){value.model=glm::scale(glm::mat4(1),glm::vec3(30,22,30));});component->updateSettings([&](auto& value){value.maxLeaves=4096;});
        component->updateSettings([&](auto& value){value.material=pbr(glm::vec3(1),.85f);});auto texture=std::make_shared<Texture>();std::vector<unsigned char> terrainPixels(1024*1024*4);
        for(uint32_t y=0;y<1024;y++)for(uint32_t x=0;x<1024;x++){float xx=x/1023.f*2-1,zz=y/1023.f*2-1,h=elevation(xx,zz),noise=.5f+.5f*std::sin(xx*140)*std::cos(zz*153);glm::vec3 color=glm::mix(glm::vec3(.20f,.31f,.11f),glm::vec3(.40f,.36f,.27f),glm::smoothstep(.15f,.38f,h));color*=.85f+.15f*noise;auto at=(y*1024+x)*4;terrainPixels[at]=uint8_t(color.r*255);terrainPixels[at+1]=uint8_t(color.g*255);terrainPixels[at+2]=uint8_t(color.b*255);terrainPixels[at+3]=255;}
        texture->setPixels(1024,1024,4,std::move(terrainPixels));component->settings().material->addTexture(texture,"material.albedo");terrain->addComponent(component);terrain->addComponent(std::make_shared<Grass>());target->addTerrain(terrain);
        atmosphere(target);sun(target,glm::vec3(3),{-.5f,-1,-.4f});manager->setting.enableSSAO=true;
    } else if (name == "clouds" || name == "clouds-sunset" || name == "clouds-storm") {
        target->setCamera(std::make_shared<Camera>(glm::vec3(0,10,0),glm::vec3(0,1,0),-90,18));
        target->mainCamera()->setZoom(70);target->mainCamera()->setExposure(1);
        atmosphere(target);
        const float angle=glm::radians(name=="clouds-sunset"?5.f:35.f),azimuth=glm::radians(name=="clouds-sunset"?20.f:120.f);
        sun(target,glm::vec3(20),{-std::cos(angle)*std::sin(azimuth),-std::sin(angle),std::cos(angle)*std::cos(azimuth)});
        target->sky()->getComponent<Cloud>()->updateSettings([&](auto& c){c.enabled=true;c.voxel=false;c.coverage=name=="clouds-storm"?.78f:.52f;c.density=name=="clouds-storm"?.01f:.007f;});
        manager->setting.enableSSAO=false;manager->setting.enableShadow=false;
    } else if (name == "cloud-volume" || name == "cloud-inside" || name == "cloud-vortex") {
        const bool inside=name=="cloud-inside";
        target->setCamera(std::make_shared<Camera>(inside?glm::vec3(-1100,2100,-3600):glm::vec3(0,1100,1800),glm::vec3(0,1,0),-90,inside?10:12));
        target->mainCamera()->setZoom(70);target->mainCamera()->setExposure(1);
        atmosphere(target);sun(target,glm::vec3(20),{-.5f,-.7f,-.3f});
        target->sky()->getComponent<Cloud>()->updateSettings([&](auto& c){c.enabled=true;c.voxel=true;c.downsample=1;c.temporal=false;c.steps=128;c.density=.004f;c.coverage=.8f;c.wind={12,4};
            if(name=="cloud-vortex"){c.storm=1;c.lightning=2;c.density=.006f;}});
        manager->setting.enableSSAO=false;manager->setting.enableShadow=false;
    } else if (name == "sky") {
        target->setCamera(std::make_shared<Camera>(glm::vec3(0,2,0),glm::vec3(0,1,0),-90,10));
        target->mainCamera()->setZoom(50);target->mainCamera()->setExposure(1);
        atmosphere(target);sun(target,glm::vec3(3),{0,-std::sin(glm::radians(10.f)),std::cos(glm::radians(10.f))});
        manager->setting.enableShadow=false;manager->setting.enableSSAO=false;
    } else if (name == "bunny") {
        target->setCamera(std::make_shared<Camera>(glm::vec3(0,3.8f,12), glm::vec3(0,1,0), -90, -10));
        const glm::vec3 colors[] = {{.78f,.83f,.86f},{.9f,.62f,.22f},{.25f,.56f,.72f}};
        for (int i = 0; i < 3; ++i)
            addMeshes(target, "Stanford Bunny " + std::to_string(i), imported("samples/assets/bunny/bun_zipper.ply", 3, true),
                      pbr(colors[i], i==1?.23f:.55f, i==1?1.f:0.f), {float(i-1)*3.1f,0,0}, glm::vec3(1), {0,15,0});
        floor(target); atmosphere(target); sun(target, {2.5f,2.4f,2.3f}, {-.5f,-1,-.4f}); point(target, {32,40,50}, {5,6,5});
    } else if (name == "bistro") {
        const std::string manifest="samples/assets/benchmarks/bistro/runtime/scene.json";
        std::ifstream input(manifest);
        if(!input)throw std::runtime_error("Run python3 tools/prepare_bistro_scene.py first");
        nlohmann::json layout;input>>layout;
        SceneImportOptions options;options.convertedTextureDirectory=layout.at("textures").get<std::string>();
        options.packedOrmSpecular=true;options.uvTangents=true;
        // Bistro's R channel is an unused zero placeholder. Falcor does not
        // consume it as AO: https://github.com/NVIDIAGameWorks/Falcor/blob/master/docs/usage/scene-formats.md
        options.packedOcclusion=false;
        const float height=layout.at("height").get<float>();
        auto importedScene=importOBJScene(layout.at("model").get<std::string>(),height,options);
        if(importedScene.triangles!=layout.at("source_triangles").get<size_t>())throw std::runtime_error("Bistro source triangle count changed");
        auto object=std::make_shared<GameObject>();object->name="Bistro complete exterior";
        object->addComponent(std::make_shared<Transform>());
        auto filter=std::make_shared<MeshFilter>();for(const auto& mesh:importedScene.meshes)filter->addMesh(mesh);
        object->addComponent(filter);auto renderer=std::make_shared<MeshRenderer>();renderer->setShader(ShaderType::PBR);
        object->addComponent(renderer);object->setDeferred(true);target->addObject(object);
        // Transform the recorded source-space camera using the same normalization as the mesh.
        auto vector=[](const auto& a){return glm::vec3(a.at(0).template get<float>(),a.at(1).template get<float>(),a.at(2).template get<float>());};
        const auto eye=(vector(layout.at("camera").at("source_position"))-importedScene.sourceOrigin)*importedScene.sourceScale;
        const auto aim=(vector(layout.at("camera").at("source_target"))-importedScene.sourceOrigin)*importedScene.sourceScale;
        const auto direction=glm::normalize(aim-eye);
        target->setCamera(std::make_shared<Camera>(eye,glm::vec3(0,1,0),glm::degrees(std::atan2(direction.z,direction.x)),glm::degrees(std::asin(direction.y))));
        target->mainCamera()->setZoom(layout.at("camera").at("fov").get<float>());
        target->mainCamera()->setExposure(layout.at("camera").at("exposure").get<float>());
        atmosphere(target);sun(target,{3.8f,3.6f,3.2f},{-.45f,-1,-.35f});
        manager->setting.clusters.enabled=true;manager->setting.clusters.pixelError=2;manager->setting.clusters.occlusion=false;
    } else if (name == "cluster-cave" || name == "cluster-cave-small") {
        const std::filesystem::path folder=name=="cluster-cave"?"samples/assets/benchmarks/procedural-cave":"samples/assets/benchmarks/procedural-cave-small";
        std::ifstream input(folder/"scene.json");
        if(!input)throw std::runtime_error("Run python3 tools/prepare_cluster_cave.py first");
        nlohmann::json layout;input>>layout;
        if(layout.at("version").get<int>()!=1)throw std::runtime_error("Unsupported procedural cave package");
        uint64_t packageBytes=0;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(folder))
            if(entry.is_regular_file())packageBytes+=entry.file_size();
        if(packageBytes>=10000000000ull)throw std::runtime_error("Procedural cave package exceeds the 10 GB limit");
        auto material=pbr(glm::vec3(1),.86f);
        material->addTexture(Texture::loadFromFileAsync((folder/"limestone-albedo.png").string(),4),"material.albedo");
        material->addTexture(Texture::loadFromFileAsync((folder/"limestone-roughness.png").string(),4),"material.roughness");
        std::vector<std::vector<std::shared_ptr<Mesh>>> prototypes;
        AssimpLoader loader;uint64_t uniqueTriangles=0;
        for(const auto& description:layout.at("meshes")){
            auto meshes=loader.loadModel(description.at("model").get<std::string>(),false);
            size_t count=0;for(const auto& mesh:meshes)count+=mesh->getIndices().size()/3;
            if(count!=description.at("triangles").get<size_t>())throw std::runtime_error("Procedural cave source geometry changed");
            for(const auto& mesh:meshes)mesh->setMaterial(material);
            uniqueTriangles+=count;prototypes.push_back(std::move(meshes));
        }
        auto vector=[](const auto& a){return glm::vec3(a.at(0).template get<float>(),a.at(1).template get<float>(),a.at(2).template get<float>());};
        uint64_t logicalTriangles=0;size_t instance=0;
        for(const auto& item:layout.at("instances")){
            const auto index=item.at("mesh").get<size_t>();
            addMeshes(target,item.at("name").get<std::string>()+" "+std::to_string(instance++),prototypes.at(index),material,
                vector(item.at("position")),vector(item.at("scale")),vector(item.at("rotation")));
            logicalTriangles+=layout.at("meshes").at(index).at("triangles").get<uint64_t>();
        }
        if(uniqueTriangles!=layout.at("unique_triangles").get<uint64_t>()||logicalTriangles!=layout.at("logical_triangles").get<uint64_t>())throw std::runtime_error("Procedural cave instance count differs from manifest");
        const auto eye=vector(layout.at("camera").at("position")),aim=vector(layout.at("camera").at("target")),direction=glm::normalize(aim-eye);
        target->setCamera(std::make_shared<Camera>(eye,glm::vec3(0,1,0),glm::degrees(std::atan2(direction.z,direction.x)),glm::degrees(std::asin(direction.y))));
        target->mainCamera()->setZoom(layout.at("camera").at("fov").get<float>());
        target->mainCamera()->setExposure(layout.at("camera").at("exposure").get<float>());
        target->mainCamera()->setMovementSpeed(7);target->mainCamera()->setClipPlanes(.1f,250);
        atmosphere(target);sun(target,{4.4f,3.8f,2.9f},{-.45f,-1,-.38f});
        point(target,{15,22,32},{0,6,-13});point(target,{24,19,12},{0,7,-37});
        manager->setting.clusters.enabled=true;manager->setting.clusters.pixelError=4;manager->setting.clusters.occlusion=true;
        manager->setting.shadowSettings.distance=180;manager->setting.shadowSettings.depthBias=.0005f;
        manager->setting.weather.fog=false;manager->setting.weather.aerialPerspective=false;
        std::cout<<"Procedural grotto: "<<instance<<" shared instances, "<<uniqueTriangles<<" unique / "<<logicalTriangles<<" logical triangles, "<<packageBytes<<" package bytes (10 GB cap)\n";
    } else if (name == "cluster-cliff") {
        std::ifstream input("samples/cluster-cliff-scene.json");
        if(!input)throw std::runtime_error("Missing cliff layout manifest");
        nlohmann::json layout;input>>layout;
        auto meshes=importOBJScene(layout.at("model").get<std::string>(),layout.at("height").get<float>()).meshes;
        size_t triangles=0;for(const auto& mesh:meshes)triangles+=mesh->getIndices().size()/3;
        if(triangles!=layout.at("source_triangles_per_instance").get<size_t>())throw std::runtime_error("Cliff benchmark requires original LOD0; run python3 tools/prepare_cluster_cliff.py");
        auto material=pbr(glm::vec3(1),.8f);const auto folder=layout.at("textures").get<std::string>();
        for(const auto& map:std::array<std::pair<const char*,const char*>,3>{{{"material.albedo","coastal_cliff_02_diff_2k.jpg"},{"material.normal","coastal_cliff_02_nor_gl_2k.jpg"},{"material.roughness","coastal_cliff_02_rough_2k.jpg"}}})
            material->addTexture(Texture::loadFromFileAsync(folder+map.second,4),map.first);
        auto vector=[](const auto& value){return glm::vec3(value.at(0).template get<float>(),value.at(1).template get<float>(),value.at(2).template get<float>());};
        size_t instance=0;for(const auto& item:layout.at("instances"))addMeshes(target,"Cliff LOD0 instance "+std::to_string(instance++),meshes,material,vector(item.at("position")),glm::vec3(1),{0,item.at("yaw").get<float>(),0});
        addMeshes(target,"Canyon floor",{quad({glm::vec3(-80,-.04f,60),{80,-.04f,60},{80,-.04f,-180},{-80,-.04f,-180}},{0,1,0})},pbr({.24f,.22f,.17f},.95f));
        const auto eye=vector(layout.at("camera").at("position")),direction=glm::normalize(vector(layout.at("camera").at("target"))-eye);
        target->setCamera(std::make_shared<Camera>(eye,glm::vec3(0,1,0),glm::degrees(std::atan2(direction.z,direction.x)),glm::degrees(std::asin(direction.y))));
        target->mainCamera()->setZoom(layout.at("camera").at("fov").get<float>());target->mainCamera()->setExposure(1.2f);
        atmosphere(target);sun(target,{3,2.9f,2.7f},{-.5f,-1,-.3f});
        manager->setting.clusters.enabled=true;manager->setting.clusters.pixelError=4;manager->setting.clusters.occlusion=false;
        std::cout<<"Cliff canyon: "<<instance<<" shared instances, "<<triangles<<" source triangles each, "<<instance*triangles<<" logical triangles\n";
    } else if (name == "dragon" || name == "buddha" || name == "armadillo") {
        std::ifstream input("samples/benchmark-assets.json");
        if(!input)throw std::runtime_error("Missing benchmark manifest; see samples/README.md");
        nlohmann::json manifest;input>>manifest;
        const auto path=manifest.at(name).at("model").get<std::string>();
        if(!std::filesystem::exists(path))throw std::runtime_error("Missing "+name+"; run python3 tools/fetch_benchmark_assets.py --scene "+name);
        const bool dragon=name=="dragon",buddha=name=="buddha";
        auto meshes=importOBJScene(path,dragon?3.f:4.f).meshes;
        size_t triangles=0;for(const auto& mesh:meshes)triangles+=mesh->getIndices().size()/3;
        std::cout<<"Classic "<<name<<": "<<meshes.size()<<" meshes, "<<triangles<<" triangles\n";
        auto material=pbr(dragon?glm::vec3(.82f,.52f,.19f):buddha?glm::vec3(.78f,.83f,.79f):glm::vec3(.38f,.43f,.5f),
                          dragon?.27f:buddha?.3f:.48f,buddha?0.f:1.f);
        addMeshes(target,name,meshes,material,{0,.35f,0},glm::vec3(1),{0,dragon?-15.f:buddha?20.f:200.f,0});
        addMeshes(target,"Scan plinth",box(),pbr({.19f,.21f,.25f},.7f),{0,.175f,0},{dragon?4.f:1.5f,.175f,dragon?2.f:1.5f});
        const glm::vec3 eye=dragon?glm::vec3(6,4.5f,9):glm::vec3(4.8f,3.2f,8);
        const auto direction=glm::normalize(glm::vec3(0,dragon?1.8f:2.2f,0)-eye);
        target->setCamera(std::make_shared<Camera>(eye,glm::vec3(0,1,0),glm::degrees(std::atan2(direction.z,direction.x)),glm::degrees(std::asin(direction.y))));
        target->mainCamera()->setZoom(48);target->mainCamera()->setExposure(1.2f);
        floor(target);atmosphere(target);sun(target,{3,2.9f,2.7f},{-.5f,-1,-.5f});point(target,{20,26,34},{4,5,4});
    } else if (name == "helmet") {
        target->setCamera(std::make_shared<Camera>(glm::vec3(4.2f,3.5f,7), glm::vec3(0,1,0), -121, -9));
        auto material = pbr(glm::vec3(1));
        const std::string folder = "samples/assets/damaged-helmet/";
        const std::pair<const char*,const char*> maps[] = {{"material.albedo","Default_albedo.jpg"},{"material.normal","Default_normal.jpg"},
            {"material.roughness","Default_metalRoughness.jpg"},{"material.metallic","Default_metalRoughness.jpg"},{"material.ao","Default_AO.jpg"}};
        for (const auto& map : maps) {
            auto texture = Texture::loadFromFileAsync(folder + map.second);
            if (!texture->pixels()) throw std::runtime_error("Missing helmet texture: " + folder + map.second);
            material->addTexture(texture,map.first);
        }
        // AssimpLoader exposes mesh-local coordinates; the glTF node rotates +90 degrees about X.
        auto meshes = imported(folder + "DamagedHelmet.gltf", 3.1f, false, glm::rotate(glm::mat4(1), glm::radians(90.f), glm::vec3(1,0,0)));
        addMeshes(target, "Damaged Helmet", meshes, material, {0,.8f,0});
        addMeshes(target, "Helmet plinth", box(), pbr({.16f,.2f,.26f}, .4f), {0,.4f,0}, {1.35f,.4f,1.35f});
        floor(target); atmosphere(target); sun(target, {2.6f,2.5f,2.4f}, {-.5f,-1,-.7f}); point(target, {45,60,80}, {-4,4,4});
    } else if (name == "cornell") {
        // Original, normalized Cornell Box-style geometry, not measured Cornell reference data.
        target->setCamera(std::make_shared<Camera>(glm::vec3(0,2.8f,9.5f), glm::vec3(0,1,0), -90, 0));
        auto white = pbr({.8f,.8f,.77f}, .8f);
        addMeshes(target,"Floor",{quad({glm::vec3(-2.8f,0,2.8f),{2.8f,0,2.8f},{2.8f,0,-2.8f},{-2.8f,0,-2.8f}}, {0,1,0})},white);
        addMeshes(target,"Ceiling",{quad({glm::vec3(-2.8f,5.6f,-2.8f),{2.8f,5.6f,-2.8f},{2.8f,5.6f,2.8f},{-2.8f,5.6f,2.8f}}, {0,-1,0})},white);
        addMeshes(target,"Back wall",{quad({glm::vec3(-2.8f,0,-2.8f),{2.8f,0,-2.8f},{2.8f,5.6f,-2.8f},{-2.8f,5.6f,-2.8f}}, {0,0,1})},white);
        addMeshes(target,"Red wall",{quad({glm::vec3(-2.8f,0,2.8f),{-2.8f,0,-2.8f},{-2.8f,5.6f,-2.8f},{-2.8f,5.6f,2.8f}}, {1,0,0})},pbr({.72f,.08f,.06f},.8f));
        addMeshes(target,"Green wall",{quad({glm::vec3(2.8f,0,-2.8f),{2.8f,0,2.8f},{2.8f,5.6f,2.8f},{2.8f,5.6f,-2.8f}}, {-1,0,0})},pbr({.12f,.62f,.18f},.8f));
        addMeshes(target,"Short box",box(),white, {-1.2f,.85f,.7f}, {.8f,.85f,.8f}, {0,-18,0});
        addMeshes(target,"Tall box",box(),white, {1.1f,1.65f,-.9f}, {.75f,1.65f,.75f}, {0,18,0});
        addMeshes(target,"Ceiling light panel",{quad({glm::vec3(-.7f,5.58f,-.5f),{.7f,5.58f,-.5f},{.7f,5.58f,.5f},{-.7f,5.58f,.5f}}, {0,-1,0})},white,glm::vec3(0),glm::vec3(1),glm::vec3(0),ShaderType::LIGHT);
        auto panelFilter=std::static_pointer_cast<MeshFilter>(target->objects().back()->GetComponent("MeshFilter"));
        auto emissive=std::make_shared<Material>(*white);emissive->setEmissiveFactor({10,9.5f,9});
        panelFilter->getMeshes()[0]->setMaterial(emissive);
        point(target, {90,85,75}, {0,5.2f,.2f});
        point(target, {6,7,8}, {0,3.2f,4.8f});
        auto spotObject = std::make_shared<GameObject>(); spotObject->name = "S0";
        auto spotTransform = std::make_shared<Transform>(); spotTransform->setPosition({0,5.2f,.2f}); spotObject->addComponent(spotTransform);
        auto spotLight = std::make_shared<SpotLight>(); spotLight->setColor({12,11,10});
        spotLight->setDirection(glm::normalize(glm::vec3(0,-1,-.15f)));
        spotLight->setCone(std::cos(glm::radians(45.f)),std::cos(glm::radians(50.f)));
        spotObject->addComponent(spotLight); target->addObject(spotObject);

    } else if (name == "sponza" || name == "san-miguel" || name == "sibenik") {
        const bool cathedral=name=="sibenik";
        std::ifstream input(cathedral?"samples/benchmark-assets.json":"samples/gi-assets.json");
        if (!input) throw std::runtime_error(cathedral?"Run python3 tools/fetch_benchmark_assets.py --scene sibenik first":"Run python3 tools/fetch_gi_assets.py first");
        nlohmann::json manifest; input >> manifest;
        const auto modelPath=manifest.at(name).at("model").get<std::string>();
        if(cathedral && !std::filesystem::exists(modelPath))throw std::runtime_error("Missing sibenik; run python3 tools/fetch_benchmark_assets.py --scene sibenik");
        const auto importedScene = importOBJScene(modelPath, 12);
        auto object = std::make_shared<GameObject>(); object->name = name;
        object->addComponent(std::make_shared<Transform>());
        auto filter = std::make_shared<MeshFilter>();
        for (const auto& mesh : importedScene.meshes) filter->addMesh(mesh);
        object->addComponent(filter);
        auto renderer = std::make_shared<MeshRenderer>();renderer->setShader(ShaderType::PBR);
        object->addComponent(renderer);object->setDeferred(true);target->addObject(object);
        if(cathedral)
            target->setCamera(std::make_shared<Camera>(glm::vec3(6,1.6f,0),glm::vec3(0,1,0),180,10));
        else if (name == "sponza")
            target->setCamera(std::make_shared<Camera>(glm::vec3(-8.5f,2.2f,0),glm::vec3(0,1,0),0,6));
        else
            target->setCamera(std::make_shared<Camera>(glm::vec3(7,2.4f,8),glm::vec3(0,1,0),-115,-3));
        target->mainCamera()->setZoom(58); target->mainCamera()->setExposure(1.1f);
        atmosphere(target);sun(target,{2.8f,2.6f,2.3f},{-.35f,-1,-.2f});
        if(cathedral){
            target->mainCamera()->setExposure(1.8f);
            point(target,{8,7,6},{4,5,0});
        }
    } else throw std::runtime_error("Unknown classic scene '" + name + "'; choose tree-single, tree-bare, tree-edge, tree-forest, underwater-dive, coastal-seabed, terrain, mountain-lake, coastal-beach, coastal-water, coastal-underwater, sky, cloud-volume, cloud-inside, cloud-vortex, clouds, clouds-sunset, clouds-storm, shadow-test, bunny, dragon, buddha, armadillo, helmet, cornell, sponza, san-miguel, sibenik, bistro, cluster-cliff, cluster-cave, cluster-cave-small, ocean, ocean-sea-state or ocean-clear");
    return target;
}
