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
    texture->width = texture->height = 4;
    texture->channels = 3;
    texture->format = texture->internalformat = GL_RGB;
    texture->data = static_cast<unsigned char*>(std::malloc(48));
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c)
            texture->data[i * 3 + c] = static_cast<unsigned char>(255 * color[c]);
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


    if (name == "terrain") {
        target->setCamera(std::make_shared<Camera>(glm::vec3(0,10,32),glm::vec3(0,1,0),-90,-12));
        target->mainCamera()->setZoom(60);target->mainCamera()->setExposure(1);
        auto terrain=std::make_shared<Terrain>();auto component=std::make_shared<TerrainComponent>();component->heightWidth=component->heightHeight=1024;component->heightData=new float[1024*1024];
        auto elevation=[](float x,float z){float h=.28f*std::exp(-((x+.35f)*(x+.35f)*5+(z+.25f)*(z+.25f)*3))+.5f*std::exp(-((x-.45f)*(x-.45f)*9+(z+.4f)*(z+.4f)*4));return h+.035f*std::sin(x*18+z*13)*std::cos(z*16)*h;};
        for(uint32_t y=0;y<1024;y++)for(uint32_t x=0;x<1024;x++)component->heightData[y*1024+x]=elevation(x/1023.f*2-1,y/1023.f*2-1);
        component->model=glm::scale(glm::mat4(1),glm::vec3(30,22,30));component->maxLeaves=4096;
        component->material=pbr(glm::vec3(1),.85f);auto texture=std::make_shared<Texture>();texture->width=texture->height=1024;texture->channels=4;texture->format=GL_RGBA;texture->data=static_cast<unsigned char*>(std::malloc(1024*1024*4));
        for(uint32_t y=0;y<1024;y++)for(uint32_t x=0;x<1024;x++){float xx=x/1023.f*2-1,zz=y/1023.f*2-1,h=elevation(xx,zz),noise=.5f+.5f*std::sin(xx*140)*std::cos(zz*153);glm::vec3 color=glm::mix(glm::vec3(.20f,.31f,.11f),glm::vec3(.40f,.36f,.27f),glm::smoothstep(.15f,.38f,h));color*=.85f+.15f*noise;auto at=(y*1024+x)*4;texture->data[at]=uint8_t(color.r*255);texture->data[at+1]=uint8_t(color.g*255);texture->data[at+2]=uint8_t(color.b*255);texture->data[at+3]=255;}
        component->material->addTexture(texture,"material.albedo");terrain->addComponent(component);terrain->addComponent(std::make_shared<Grass>());target->addTerrain(terrain);
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
    } else if (name == "helmet") {
        target->setCamera(std::make_shared<Camera>(glm::vec3(4.2f,3.5f,7), glm::vec3(0,1,0), -121, -9));
        auto material = pbr(glm::vec3(1));
        const std::string folder = "samples/assets/damaged-helmet/";
        const std::pair<const char*,const char*> maps[] = {{"material.albedo","Default_albedo.jpg"},{"material.normal","Default_normal.jpg"},
            {"material.roughness","Default_metalRoughness.jpg"},{"material.metallic","Default_metalRoughness.jpg"},{"material.ao","Default_AO.jpg"}};
        for (const auto& map : maps) {
            auto texture = Texture::loadFromFileAsync(folder + map.second);
            if (!texture->data) throw std::runtime_error("Missing helmet texture: " + folder + map.second);
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

    } else if (name == "sponza" || name == "san-miguel") {
        std::ifstream input("samples/gi-assets.json");
        if (!input) throw std::runtime_error("Run python3 tools/fetch_gi_assets.py first");
        nlohmann::json manifest; input >> manifest;
        const auto importedScene = importOBJScene(manifest.at(name).at("model").get<std::string>(), 12);
        auto object = std::make_shared<GameObject>(); object->name = name;
        object->addComponent(std::make_shared<Transform>());
        auto filter = std::make_shared<MeshFilter>();
        for (const auto& mesh : importedScene.meshes) filter->addMesh(mesh);
        object->addComponent(filter);
        auto renderer = std::make_shared<MeshRenderer>();renderer->setShader(ShaderType::PBR);
        object->addComponent(renderer);object->setDeferred(true);target->addObject(object);
        if (name == "sponza")
            target->setCamera(std::make_shared<Camera>(glm::vec3(-8.5f,2.2f,0),glm::vec3(0,1,0),0,6));
        else
            target->setCamera(std::make_shared<Camera>(glm::vec3(7,2.4f,8),glm::vec3(0,1,0),-115,-3));
        target->mainCamera()->setZoom(58); target->mainCamera()->setExposure(1.1f);
        atmosphere(target);sun(target,{2.8f,2.6f,2.3f},{-.35f,-1,-.2f});
        manager->setting.enableRSM = true;
    } else throw std::runtime_error("Unknown classic scene '" + name + "'; choose terrain, sky, bunny, helmet, cornell, sponza, san-miguel, ocean or ocean-clear");
    return target;
}
