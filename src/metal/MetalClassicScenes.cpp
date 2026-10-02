#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "metal/MetalDemo.h"
#include "metal/MetalBackend.h"
#include "metal/MetalSceneImport.h"
#include "component/Model.h"
#include "component/GameObject.h"
#include "component/transform.h"
#include "component/Mesh_Filter.h"
#include "component/Mesh_Renderer.h"
#include "component/Lights.h"
#include "component/Atmosphere.h"
#include "component/Ocean.h"
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

extern std::shared_ptr<RenderScene> scene;
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
    transform->position = position; transform->scale = scale; transform->rotation = rotation;
    object->addComponent(transform);
    auto filter = std::make_shared<MeshFilter>();
    for (const auto& mesh : meshes) { mesh->material = material; filter->addMesh(mesh); }
    object->addComponent(filter);
    auto renderer = std::make_shared<MeshRenderer>(); renderer->shader = RenderManager::GetInstance()->getShader(shader);
    object->addComponent(renderer); object->setDeferred(shader == ShaderType::PBR); target->addObject(object);
}
std::vector<std::shared_ptr<Mesh>> imported(const std::string& path, float height, bool generateUV, glm::mat4 transform = glm::mat4(1)) {
    AssimpLoader loader;
    auto meshes = loader.loadModel(path, true);
    if (meshes.empty()) throw std::runtime_error("Cannot import " + path + "; run python3 tools/fetch_classic_assets.py");
    for (const auto& mesh : meshes) for (auto& v : mesh->vertices) {
        v.Position = glm::vec3(transform * glm::vec4(v.Position, 1));
        v.Normal = glm::normalize(glm::mat3(transform) * v.Normal);
    }
    glm::vec3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
    for (const auto& mesh : meshes) for (const auto& v : mesh->vertices) {
        low = glm::min(low, v.Position); high = glm::max(high, v.Position);
    }
    float scale = height / (high.y - low.y);
    auto center = glm::vec3((low.x+high.x)/2, low.y, (low.z+high.z)/2);
    for (const auto& mesh : meshes) for (auto& v : mesh->vertices) {
        // PLY scans have no UVs. Planar UVs keep the derivative normal-map basis defined.
        if (generateUV) v.TexCoords = {v.Position.x * 12, v.Position.z * 12};
        v.Position = (v.Position - center) * scale;
        glm::vec3 axis = std::abs(v.Normal.y) < .95f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
        v.Tangent = glm::normalize(glm::cross(axis, v.Normal));
        v.Bitangent = glm::cross(v.Normal, v.Tangent);
    }
    return meshes;
}
void sun(const std::shared_ptr<RenderScene>& target, glm::vec3 color, glm::vec3 direction) {
    auto object = std::make_shared<GameObject>(); object->name = "Gallery sun";
    object->addComponent(std::make_shared<Transform>());
    auto light = std::make_shared<DirectionLight>(); light->data.color = color; light->data.direction = glm::normalize(direction);
    object->addComponent(light); target->addObject(object);
}
void point(const std::shared_ptr<RenderScene>& target, glm::vec3 color, glm::vec3 position) {
    auto object = std::make_shared<GameObject>(); object->name = "Gallery point light";
    auto transform = std::make_shared<Transform>(); transform->position = position; object->addComponent(transform);
    auto light = std::make_shared<PointLight>(); light->data.color = color;
    object->addComponent(light); target->addObject(object);
}
void atmosphere(const std::shared_ptr<RenderScene>& target) {
    auto sky = std::make_shared<Sky>(); sky->addComponent(std::make_shared<Atmosphere>());
    sky->skybox->initDone = false; sky->skybox->addTexture(std::make_shared<Texture>(), "skybox");
    sky->width = sky->height = 4;
    for (int i = 0; i < 6; ++i) { sky->data[i] = static_cast<unsigned char*>(std::malloc(48)); std::fill(sky->data[i], sky->data[i]+48, 16); }
    target->sky = sky;
}
void floor(const std::shared_ptr<RenderScene>& target) {
    addMeshes(target, "Gallery floor", {quad({glm::vec3(-30,0,30),{30,0,30},{30,0,-30},{-30,0,-30}}, {0,1,0})}, pbr({.32f,.36f,.41f}, .75f));
}
}
std::shared_ptr<RenderScene> makeMetalClassicScene(const std::string& name) {
    if(name=="ocean" || name=="ocean-clear")return makeMetalOceanScene(name=="ocean-clear");
    auto target = std::make_shared<RenderScene>();
    auto manager = RenderManager::GetInstance();
    manager->setting.enableShadow = true; manager->setting.enableSSAO = true; manager->setting.enableRSM = false;
    // Shadow attachments belong to the lights in each scene.
    manager->shadowPass = std::make_shared<ShadowPass>();
    manager->rsmPass = std::make_shared<RSMPass>();
    if (name == "bunny") {
        target->main_camera = std::make_shared<Camera>(glm::vec3(0,3.8f,12), glm::vec3(0,1,0), -90, -10);
        const glm::vec3 colors[] = {{.78f,.83f,.86f},{.9f,.62f,.22f},{.25f,.56f,.72f}};
        for (int i = 0; i < 3; ++i)
            addMeshes(target, "Stanford Bunny " + std::to_string(i), imported("samples/assets/bunny/bun_zipper.ply", 3, true),
                      pbr(colors[i], i==1?.23f:.55f, i==1?1.f:0.f), {float(i-1)*3.1f,0,0}, glm::vec3(1), {0,15,0});
        floor(target); atmosphere(target); sun(target, {2.5f,2.4f,2.3f}, {-.5f,-1,-.4f}); point(target, {32,40,50}, {5,6,5});
    } else if (name == "helmet") {
        target->main_camera = std::make_shared<Camera>(glm::vec3(4.2f,3.5f,7), glm::vec3(0,1,0), -121, -9);
        auto material = pbr(glm::vec3(1));
        const std::string folder = "samples/assets/damaged-helmet/";
        const std::pair<const char*,const char*> maps[] = {{"material.albedo","Default_albedo.jpg"},{"material.normal","Default_normal.jpg"},
            {"material.roughness","Default_metalRoughness.jpg"},{"material.metallic","Default_metalRoughness.jpg"},{"material.ao","Default_AO.jpg"}};
        for (const auto& map : maps) {
            auto texture = Texture::loadFromFileAsync(folder + map.second);
            if (!texture->data) throw std::runtime_error("Missing helmet texture: " + folder + map.second);
            material->textures[map.first] = texture;
        }
        // AssimpLoader exposes mesh-local coordinates; the glTF node rotates +90 degrees about X.
        auto meshes = imported(folder + "DamagedHelmet.gltf", 3.1f, false, glm::rotate(glm::mat4(1), glm::radians(90.f), glm::vec3(1,0,0)));
        addMeshes(target, "Damaged Helmet", meshes, material, {0,.8f,0});
        addMeshes(target, "Helmet plinth", box(), pbr({.16f,.2f,.26f}, .4f), {0,.4f,0}, {1.35f,.4f,1.35f});
        floor(target); atmosphere(target); sun(target, {2.6f,2.5f,2.4f}, {-.5f,-1,-.7f}); point(target, {45,60,80}, {-4,4,4});
    } else if (name == "cornell") {
        // Original, normalized Cornell Box-style geometry, not measured Cornell reference data.
        target->main_camera = std::make_shared<Camera>(glm::vec3(0,2.8f,9.5f), glm::vec3(0,1,0), -90, 0);
        auto white = pbr({.8f,.8f,.77f}, .8f);
        addMeshes(target,"Floor",{quad({glm::vec3(-2.8f,0,2.8f),{2.8f,0,2.8f},{2.8f,0,-2.8f},{-2.8f,0,-2.8f}}, {0,1,0})},white);
        addMeshes(target,"Ceiling",{quad({glm::vec3(-2.8f,5.6f,-2.8f),{2.8f,5.6f,-2.8f},{2.8f,5.6f,2.8f},{-2.8f,5.6f,2.8f}}, {0,-1,0})},white);
        addMeshes(target,"Back wall",{quad({glm::vec3(-2.8f,0,-2.8f),{2.8f,0,-2.8f},{2.8f,5.6f,-2.8f},{-2.8f,5.6f,-2.8f}}, {0,0,1})},white);
        addMeshes(target,"Red wall",{quad({glm::vec3(-2.8f,0,2.8f),{-2.8f,0,-2.8f},{-2.8f,5.6f,-2.8f},{-2.8f,5.6f,2.8f}}, {1,0,0})},pbr({.72f,.08f,.06f},.8f));
        addMeshes(target,"Green wall",{quad({glm::vec3(2.8f,0,-2.8f),{2.8f,0,2.8f},{2.8f,5.6f,2.8f},{2.8f,5.6f,-2.8f}}, {-1,0,0})},pbr({.12f,.62f,.18f},.8f));
        addMeshes(target,"Short box",box(),white, {-1.2f,.85f,.7f}, {.8f,.85f,.8f}, {0,-18,0});
        addMeshes(target,"Tall box",box(),white, {1.1f,1.65f,-.9f}, {.75f,1.65f,.75f}, {0,18,0});
        addMeshes(target,"Ceiling light panel",{quad({glm::vec3(-.7f,5.58f,-.5f),{.7f,5.58f,-.5f},{.7f,5.58f,.5f},{-.7f,5.58f,.5f}}, {0,-1,0})},white,glm::vec3(0),glm::vec3(1),glm::vec3(0),ShaderType::LIGHT);
        auto panel = std::static_pointer_cast<MeshRenderer>(target->objects.back()->GetComponent("MeshRenderer"));
        panel->shader = std::make_shared<Shader>("./src/shader/light.vs", "./src/shader/samples/emissive.fs");
        panel->shader->requireMat = false;
        panel->shader->setVec3("emissionColor", glm::vec3(10,9.5f,9));
        point(target, {90,85,75}, {0,5.2f,.2f});
        point(target, {6,7,8}, {0,3.2f,4.8f});
        auto spotObject = std::make_shared<GameObject>(); spotObject->name = "S0";
        auto spotTransform = std::make_shared<Transform>(); spotTransform->position = {0,5.2f,.2f}; spotObject->addComponent(spotTransform);
        auto spotLight = std::make_shared<SpotLight>(); spotLight->data.color = {12,11,10};
        spotLight->data.direction = glm::normalize(glm::vec3(0,-1,-.15f));
        spotLight->data.cutOff = std::cos(glm::radians(45.f)); spotLight->data.outerCutOff = std::cos(glm::radians(50.f));
        spotObject->addComponent(spotLight); target->addObject(spotObject);
        manager->setting.enableRSM = true;

    } else if (name == "sponza" || name == "san-miguel") {
        std::ifstream input("samples/gi-assets.json");
        if (!input) throw std::runtime_error("Run python3 tools/fetch_gi_assets.py first");
        nlohmann::json manifest; input >> manifest;
        const auto importedScene = importMetalOBJScene(manifest.at(name).at("model").get<std::string>(), 12);
        auto object = std::make_shared<GameObject>(); object->name = name;
        object->addComponent(std::make_shared<Transform>());
        auto filter = std::make_shared<MeshFilter>();
        for (const auto& mesh : importedScene.meshes) filter->addMesh(mesh);
        object->addComponent(filter);
        auto renderer = std::make_shared<MeshRenderer>();renderer->shader = manager->getShader(ShaderType::PBR);
        object->addComponent(renderer);object->setDeferred(true);target->addObject(object);
        if (name == "sponza")
            target->main_camera = std::make_shared<Camera>(glm::vec3(-8.5f,2.2f,0),glm::vec3(0,1,0),0,6);
        else
            target->main_camera = std::make_shared<Camera>(glm::vec3(7,2.4f,8),glm::vec3(0,1,0),-115,-3);
        target->main_camera->Zoom = 58; target->main_camera->exposure = 1.1f;
        atmosphere(target);sun(target,{2.8f,2.6f,2.3f},{-.35f,-1,-.2f});
        manager->setting.enableRSM = true;
    } else throw std::runtime_error("Unknown classic scene '" + name + "'; choose bunny, helmet, cornell, sponza, san-miguel, ocean or ocean-clear");
    return target;
}
void renderMetalGallery(const std::string& directory, const std::string& selected) {
    std::filesystem::create_directories(directory);
    const bool oceanGallery=selected=="ocean" || selected=="ocean-clear";
    const int width=oceanGallery?1920:960,height=oceanGallery?1080:720;
    constexpr int accumulationFrames=16;
    MetalBackend::initialize(nullptr, width, height);
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    auto input = InputManager::GetInstance(); input->width = width; input->height = height;
    auto manager = RenderManager::GetInstance(); manager->init();
    std::vector<std::string> names;
    if (selected == "gi") names = {"sponza", "san-miguel"};
    else if (selected.empty() || selected == "core") {
        names = {"cornell", "bunny", "helmet"};
        if (selected.empty() && std::filesystem::exists("samples/assets/gi/san-miguel/san-miguel-low-poly.obj") && std::filesystem::exists("samples/assets/gi/sponza/sponza.obj"))
            names.insert(names.end(), {"sponza", "san-miguel"});
    } else names = {selected};
    bool capturedGPU = false;
    for (const std::string& name : names) {
        MetalBackend::FloatTexture direct{};
        scene = makeMetalClassicScene(name);
        manager->temporalAA->reset();
        if(name=="ocean" || name=="ocean-clear") {auto water=std::static_pointer_cast<Ocean>(scene->terrain->GetComponent("Ocean"));water->animate=false;water->inner_time=8;}
        if (name == "sponza" || name == "san-miguel") {
            manager->setting.enableRSM = false;
            for (int frame=0;frame<accumulationFrames;++frame) {
                MetalBackend::beginFrame();manager->render(scene);
                if (frame==accumulationFrames-1) MetalBackend::capture((std::filesystem::path(directory)/(name+"-direct.png")).string().c_str());
                MetalBackend::present();
            }
            direct = MetalBackend::readFloatTexture(manager->deferredPass->postTexture->id);
            manager->setting.enableRSM = true;
        }
        for (int frame = 0; frame < accumulationFrames; ++frame) {
            const char* trace = std::getenv("SR_METAL_CAPTURE_PATH");
            const bool captureGPU = frame == accumulationFrames-1 && trace && !capturedGPU;
            if (captureGPU) MetalBackend::beginGPUCapture(trace);
            MetalBackend::beginFrame(); manager->render(scene);
            if (frame == accumulationFrames-1) {
                const auto output = (std::filesystem::path(directory) / (name + ".png")).string();
                MetalBackend::inspectTexture(manager->temporalAA->outputTexture(), ("build/metal-gallery-" + name + "-tsaa.png").c_str());
                MetalBackend::inspectTexture(manager->deferredPass->postTexture->id, ("build/metal-gallery-" + name + "-hdr.png").c_str());
                if (manager->setting.enableRSM)
                    MetalBackend::inspectTexture(manager->rsmPass->outTexture->id, ("build/metal-gallery-" + name + "-rsm.png").c_str());
                if(name!="ocean" && name!="ocean-clear")MetalBackend::inspectTexture(manager->deferredPass->gNormal->id, ("build/metal-gallery-" + name + "-normal.png").c_str());
                if (scene->sky) {
                    auto sky = std::static_pointer_cast<Atmosphere>(scene->sky->GetComponent("Atmosphere"));
                    MetalBackend::inspectTexture(sky->skyViewTexture->tex->id, ("build/metal-gallery-" + name + "-sky.png").c_str());
                }
                if (!direct.rgba.empty()) {
                    auto combined = MetalBackend::readFloatTexture(manager->rsmPass->outTexture->id);
                    double baseMean = 0, deltaMean = 0; size_t changed = 0;
                    for (size_t i=0;i<direct.rgba.size();i+=4) {
                        bool positive = false;
                        for (int c=0;c<3;++c) {
                            baseMean += direct.rgba[i+c];
                            double delta = combined.rgba[i+c]-direct.rgba[i+c];
                            deltaMean += delta; positive |= delta > 1e-4;
                        }
                        changed += positive;
                    }
                    const double components = direct.width * direct.height * 3;
                    std::cout << name << " linear HDR baseMean=" << baseMean/components
                              << " indirectMean=" << deltaMean/components
                              << " ratio=" << deltaMean/baseMean << " changedPixels=" << changed << '\n';
                }
                MetalBackend::capture(output.c_str()); std::cout << "Rendered " << output << '\n';
            }
            MetalBackend::present();
            if (captureGPU) { MetalBackend::endGPUCapture(); capturedGPU = true; }
        }
        if(name=="ocean" || name=="ocean-clear") {
            auto water=std::static_pointer_cast<Ocean>(scene->terrain->GetComponent("Ocean"));
            // Report the main spectrum in metres so preset tuning can be compared objectively.
            auto displacement=MetalBackend::readFloatTexture(water->DisplaceRT_Texture->tex->id);
            double heightSum=0,heightSquaredSum=0;
            float heightMin=std::numeric_limits<float>::max(),heightMax=-heightMin;
            for(size_t i=0;i<displacement.rgba.size();i+=4) {
                const float h=displacement.rgba[i+1];
                if(!std::isfinite(h))throw std::runtime_error("Non-finite ocean height");
                heightMin=std::min(heightMin,h);heightMax=std::max(heightMax,h);
                heightSum+=h;heightSquaredSum+=double(h)*h;
            }
            const double sampleCount=displacement.width*displacement.height;
            if(sampleCount==0)throw std::runtime_error("Missing ocean displacement readback");
            const double mean=heightSum/sampleCount;
            std::cout<<name<<" main-spectrum height min="<<heightMin<<" max="<<heightMax
                     <<" stddev="<<std::sqrt(std::max(0.0,heightSquaredSum/sampleCount-mean*mean))<<" metres\n";
            auto reference=MetalBackend::readFloatTexture(manager->deferredPass->postTexture->id);
            auto comparison=[&](const char* suffix) {
                manager->temporalAA->reset();
                for(int i=0;i<accumulationFrames-1;++i){MetalBackend::beginFrame();manager->render(scene);MetalBackend::present();}
                MetalBackend::beginFrame();manager->render(scene);
                auto variant=MetalBackend::readFloatTexture(manager->deferredPass->postTexture->id);
                double maxDelta=0;
                for(size_t i=0;i<variant.rgba.size();i+=4)for(int c=0;c<3;++c) {
                    if(!std::isfinite(variant.rgba[i+c]))throw std::runtime_error("Non-finite ocean comparison");
                    maxDelta=std::max(maxDelta,double(std::abs(variant.rgba[i+c]-reference.rgba[i+c])));
                }
                if(maxDelta<1e-5)throw std::runtime_error("Ocean toggle did not change rendered output");
                MetalBackend::capture((std::filesystem::path(directory)/(name+suffix+".png")).string().c_str());
                MetalBackend::present();std::cout<<name<<suffix<<" max HDR difference="<<maxDelta<<'\n';
            };
            if(name=="ocean") {
                water->detailWaves=false;comparison("-no-detail");water->detailWaves=true;
                water->subsurfaceStrength=0;comparison("-no-scattering");water->subsurfaceStrength=1;
            } else {
                water->refraction=false;comparison("-opaque");water->refraction=true;
            }
        }
        if (!direct.rgba.empty()) {
            manager->rsmPass->indirectOnly = true;
            manager->temporalAA->reset();
            for(int i=0;i<accumulationFrames-1;++i){MetalBackend::beginFrame();manager->render(scene);MetalBackend::present();}
            MetalBackend::beginFrame();manager->render(scene);
            MetalBackend::capture((std::filesystem::path(directory)/(name+"-indirect.png")).string().c_str());
            MetalBackend::inspectTexture(manager->rsmPass->outTexture->id,("build/metal-gallery-"+name+"-indirect.png").c_str());
            MetalBackend::present();
            for (int source = 0; source < 2; ++source) {
                manager->rsmPass->sunBounce = source == 0;
                manager->rsmPass->skyBounce = source == 1;
                MetalBackend::beginFrame();manager->render(scene);
                MetalBackend::present();manager->temporalAA->reset();
                for(int i=0;i<accumulationFrames-1;++i){MetalBackend::beginFrame();manager->render(scene);MetalBackend::present();}
                MetalBackend::beginFrame();manager->render(scene);
                const std::string label = source == 0 ? "sun-indirect" : "sky-indirect";
                MetalBackend::capture((std::filesystem::path(directory)/(name+"-"+label+".png")).string().c_str());
                auto raw = MetalBackend::readFloatTexture(manager->rsmPass->outTexture->id);
                double mean = 0;
                for(size_t i=0;i<raw.rgba.size();i+=4)for(int c=0;c<3;++c)mean+=raw.rgba[i+c];
                std::cout<<name<<" "<<label<<" mean="<<mean/(raw.width*raw.height*3)<<'\n';
                MetalBackend::present();
            }
            manager->rsmPass->sunBounce = manager->rsmPass->skyBounce = true;
            manager->rsmPass->indirectOnly = false;
        }
    }
    MetalBackend::shutdown();
}
