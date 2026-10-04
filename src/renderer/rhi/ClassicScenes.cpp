#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "renderer/rhi/FeatureScenes.h"
#include "renderer/SceneImport.h"
#include "component/Model.h"
#include "component/GameObject.h"
#include "component/transform.h"
#include "component/Mesh_Filter.h"
#include "component/Mesh_Renderer.h"
#include "component/Lights.h"
#include "component/Atmosphere.h"
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
    sky->skybox->setInitialized(false); sky->skybox->addTexture(std::make_shared<Texture>(), "skybox");
    sky->width = sky->height = 4;
    for (int i = 0; i < 6; ++i) { sky->data[i] = static_cast<unsigned char*>(std::malloc(48)); std::fill(sky->data[i], sky->data[i]+48, 16); }
    target->addSky(sky);
}
void floor(const std::shared_ptr<RenderScene>& target) {
    addMeshes(target, "Gallery floor", {quad({glm::vec3(-30,0,30),{30,0,30},{30,0,-30},{-30,0,-30}}, {0,1,0})}, pbr({.32f,.36f,.41f}, .75f));
}
}
std::shared_ptr<RenderScene> render::makeClassicScene(const std::string& name) {
    if(name=="ocean" || name=="ocean-clear")return makeOceanScene(name=="ocean-clear");
    auto target = std::make_shared<RenderScene>();
    auto manager = RenderManager::GetInstance();
    manager->setting.enableShadow = true; manager->setting.enableSSAO = true; manager->setting.enableRSM = false;
    // Shadow attachments belong to the lights in each scene.


    if (name == "mountain-lake" || name == "mountain-lake-ground" || name == "mountain-lake-beach") {
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
        target->mainCamera()->setZoom(62); target->mainCamera()->setExposure(1.4f);
        atmosphere(target); sun(target,glm::vec3(3),{-.5f,-1,-.4f});
        manager->setting.enableSSAO = true;
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
        manager->setting.enableRSM = true;

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
        manager->setting.enableRSM = true;
    } else throw std::runtime_error("Unknown classic scene '" + name + "'; choose terrain, mountain-lake, sky, bunny, dragon, buddha, armadillo, helmet, cornell, sponza, san-miguel, sibenik, ocean or ocean-clear");
    return target;
}
