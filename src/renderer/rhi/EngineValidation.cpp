#include "renderer/rhi/FeatureScenes.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/RenderScene.h"
#include "component/TerrainComponent.h"
#include "component/Grass.h"
#include "component/GameObject.h"
#include "component/Mesh_Filter.h"
#include "component/Transform.h"
#include "renderer/Material.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "renderer/rhi/GpuImageCache.h"
#include "renderer/rhi/GraphTextures.h"
#include "component/Lights.h"
#include "engine/RenderRuntime.h"
#include "rhi/ShaderAssets.h"
#include "object/Terrain.h"
#include "utils/Camera.h"
#include "system/Loader.h"
#include "engine/AssetPath.h"
#include "engine/FixedStepClock.h"
#include "system/ResourceManager.h"
#include "renderer/Texture.h"
#include <glad/glad.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <type_traits>
#include <new>
#include <cstring>
namespace render {
void validateEngineBasics(std::shared_ptr<rhi::GraphicsDevice> device) {
    auto check = [](bool value, const char *reason) {
        if (!value)
            throw std::runtime_error(reason);
    };
    {
        // Checked write boundaries reject before mutation and maintain independent versions.
        Camera camera;
        camera.setClipPlanes(.25f, 320.f);
        auto projection = camera.GetPerspective();
        check(std::abs(projection[2][2] - (-(320.f + .25f) / (320.f - .25f))) < 1e-6f,
              "Camera projection ignored configured clip planes");
        bool rejected = false;
        try {
            camera.setClipPlanes(20, 10);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && camera.getNear() == .25f && camera.getFar() == 320.f,
              "Invalid camera range partially committed");
        camera.setFixed(true);
        auto position = camera.getPosition();
        camera.ProcessKeyboard(Camera_Movement::FORWARD, 1);
        check(camera.getPosition() == position, "Fixed camera continued moving");
        auto light = std::make_shared<DirectionLight>();
        auto data = light->getData();
        auto revision = light->getContentRevision();
        light->setDirection({2, -2, 0});
        check(std::abs(glm::length(light->getData().direction) - 1) < 1e-6f &&
                  light->getContentRevision() > revision && light->isDirty(),
              "Light setter did not normalize, invalidate or dirty the light");
        rejected = false;
        data.direction = {0, 0, 0};
        try {
            light->setData(data);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && glm::length(light->getData().direction) > .99f,
              "Invalid light partially committed");
        auto spot = std::make_shared<SpotLight>();
        auto cone = spot->getData();
        rejected = false;
        try {
            spot->setCone(.5f, .6f);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && spot->getData().cutOff == cone.cutOff, "Invalid cone partially committed");
        auto mesh = Mesh::initPlane();
        auto material = std::make_shared<Material>();
        mesh->setMaterial(material);
        auto old = mesh->getVertices();
        revision = mesh->getContentRevision();
        rejected = false;
        try {
            mesh->setGeometry(old, {UINT32_MAX});
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && mesh->getContentRevision() == revision && mesh->getVertices().size() == old.size(),
              "Invalid geometry polluted mesh content or version");
        auto parameters = material->properties();
        auto scalarRevision = material->parameterRevision();
        auto imageRevision = material->getContentRevision();
        parameters.roughnessFactor = .2f;
        material->setProperties(parameters);
        check(material->parameterRevision() > scalarRevision &&
                  material->getContentRevision() == imageRevision,
              "Scalar material edit rebuilt texture content");
        parameters.opacityFactor = std::numeric_limits<float>::quiet_NaN();
        rejected = false;
        try {
            material->setProperties(parameters);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && material->getOpacityFactor() == 1,
              "Invalid material parameters partially committed");
        auto first = std::make_shared<Texture>(), replacement = std::make_shared<Texture>();
        material->addTexture(first, "material.albedo");
        revision = material->getContentRevision();
        material->addTexture(replacement, "material.albedo");
        check(material->getTextures().at("material.albedo") == replacement &&
                  material->getContentRevision() > revision,
              "Replacing texture silently retained old content or version");
        auto copy = std::make_shared<Material>(*material);
        check(copy->assetId != material->assetId && copy->getTextures().at("material.albedo") == replacement,
              "Asset copy lost distinct identity or source texture");
        auto foreign = std::async(std::launch::async, [&] {
            unsigned denied = 0;
            try {
                camera.setExposure(2);
            } catch (const std::logic_error &) {
                ++denied;
            }
            try {
                light->setColor({2, 2, 2});
            } catch (const std::logic_error &) {
                ++denied;
            }
            try {
                mesh->getVertices();
            } catch (const std::logic_error &) {
                ++denied;
            }
            try {
                material->setRoughnessFactor(.7f);
            } catch (const std::logic_error &) {
                ++denied;
            }
            try {
                Material illegal(*material);
            } catch (const std::logic_error &) {
                ++denied;
            }
            return denied == 5;
        });
        check(foreign.get(), "Private asset or camera API accepted foreign-thread access");
    }
    {
        auto cache = GpuImageCache::forDevice(device);
        check(cache == GpuImageCache::forDevice(device), "Device created separate GPU image caches");
        cache->setIdleBudget(0);
        auto before = cache->stats();
        auto image =
            std::make_shared<const ImageRGBA8>(ImageRGBA8{2, 1, {17, 31, 67, 255, 79, 83, 101, 255}});
        auto clone = std::make_shared<const ImageRGBA8>(*image);
        auto a = cache->acquire(image), b = cache->acquire(image), c = cache->acquire(clone);
        check(a == b && b == c && cache->stats().uploads == before.uploads + 1,
              "Shared or equal images were uploaded more than once");
        check(device->readTexture(a->texture()) == image->pixels, "Shared image changed uploaded pixels");
        check(cache->missingBytes({image, clone}) == 0, "Image admission charged cached content twice");
        auto changed = std::make_shared<ImageRGBA8>(*image);
        changed->pixels[0]++;
        auto d = cache->acquire(changed);
        auto shape = std::make_shared<ImageRGBA8>(*image);
        shape->width = 1;
        shape->height = 2;
        auto e = cache->acquire(shape);
        check(d != a && e != a && cache->stats().uploads == before.uploads + 3,
              "GPU image cache aliased different content or dimensions");
        auto invalid = std::make_shared<ImageRGBA8>(*image);
        invalid->pixels.pop_back();
        bool rejected = false;
        try {
            cache->acquire(invalid);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && cache->stats().uploads == before.uploads + 3, "Invalid image polluted GPU cache");
        {
            // An equal-content alias can die while the GPU entry retains a
            // different source. Reuse that CPU address with new content.
            using Storage = std::aligned_storage_t<sizeof(ImageRGBA8), alignof(ImageRGBA8)>;
            auto storage = std::make_shared<Storage>();
            auto makeAlias = [&](ImageRGBA8 value) {
                auto pointer = new (storage.get()) ImageRGBA8(std::move(value));
                return std::shared_ptr<const ImageRGBA8>(
                    pointer, [storage](const ImageRGBA8 *p) { p->~ImageRGBA8(); });
            };
            auto alias = makeAlias(*image);
            check(cache->acquire(alias) == a, "Equal-content alias failed to share its GPU image");
            alias.reset();
            auto different = *image;
            different.pixels[2]++;
            alias = makeAlias(std::move(different));
            auto recycled = cache->acquire(alias);
            check(recycled != a && device->readTexture(recycled->texture()) == alias->pixels,
                  "Reused CPU address hit stale GPU image content");
            recycled.reset();
            alias.reset();
            cache->trim();
        }
        auto foreign = std::async(std::launch::async, [&] {
            try {
                cache->acquire(image);
            } catch (const std::logic_error &) {
                return true;
            }
            return false;
        });
        check(foreign.get(), "GPU image cache accepted a foreign-thread upload");
        cache->trim();
        check(cache->stats().residentBytes >= 24 && cache->stats().idleBytes == 0,
              "Budget evicted live GPU leases");
        a.reset();
        b.reset();
        c.reset();
        cache->setIdleBudget(8);
        auto idle = cache->acquire(image);
        idle.reset();
        cache->trim();
        check(cache->stats().idleBytes == 8, "Idle cache did not retain one budgeted image");
        auto newest = std::make_shared<ImageRGBA8>(*image);
        newest->pixels[1]++;
        auto lease = cache->acquire(newest);
        lease.reset();
        cache->trim();
        check(cache->stats().idleBytes == 8 && cache->missingBytes({image}) == 8 &&
                  cache->missingBytes({newest}) == 0,
              "Idle cache did not evict the least recently used image");
        d.reset();
        e.reset();
        cache->setIdleBudget(0);
        check(cache->stats().idleBytes == 0, "Zero idle budget retained unused GPU images");
        {
            MaterialDesc desc;
            desc.sharedImages[0] = image;
            auto first = std::make_shared<GpuMaterial>(device, desc);
            const auto uploads = cache->stats().uploads;
            desc.filter = rhi::Filter::Nearest;
            auto second = std::make_shared<GpuMaterial>(device, desc);
            check(cache->stats().uploads == uploads && GpuMaterial::imageUploadBytes(device, desc) == 0,
                  "Material slots or sampler choice prevented GPU image sharing");
        }
        cache->trim();
        device->waitIdle();
        cache->setIdleBudget(64 * 1024 * 1024);
        {
            auto live = cache->acquire(image);
            auto idle = cache->acquire(newest);
            idle.reset();
            device->waitIdle();
            const auto before = device->resourceMemory();
            const auto evictions = cache->stats().evictions;
            device->setResourceBudget(before.usedBytes());
            auto buffer = device->createBuffer({8, rhi::BufferUsage::Vertex, "Image pressure admission"});
            check(cache->stats().idleBytes == 0 && cache->stats().evictions > evictions &&
                      device->resourceMemory().pressureRecoveries == before.pressureRecoveries + 1 &&
                      device->readTexture(live->texture()) == image->pixels,
                  "Pressure eviction failed to reclaim idle images or evicted a live material lease");
            device->destroyBuffer(buffer);
            device->setResourceBudget(before.budgetBytes);
        }
        cache->releaseIdle();
        device->waitIdle();
    }
    {
        auto scene = std::make_shared<RenderScene>();
        auto object = std::make_shared<GameObject>("structural light");
        object->addComponent(std::make_shared<DirectionLight>());
        scene->addObject(object);
        struct SpoofLight final : Component {
            SpoofLight() { name = "DirectionLight"; }
        };
        auto spoof = std::make_shared<SpoofLight>();
        bool collision = false;
        try {
            object->addComponent(spoof);
        } catch (const std::logic_error &) {
            collision = true;
        }
        check(collision && !spoof->hasOwner() &&
                  object->getComponent<Light>() == object->getComponent<DirectionLight>(),
              "Component reflection name collision corrupted typed lookup or base query");
        const auto revision = scene->revision();
        scene->addObject(object);
        check(scene->objects().size() == 1 && scene->directionLights().size() == 1 &&
                  scene->revision() == revision,
              "Duplicate insertion duplicated world or light index");
        auto foreign = std::async(std::launch::async, [&] {
            bool read = false, write = false;
            try {
                scene->objects();
            } catch (const std::logic_error &) {
                read = true;
            }
            try {
                scene->setCamera({});
            } catch (const std::logic_error &) {
                write = true;
            }
            return read && write;
        });
        check(foreign.get(), "Private scene structure bypassed its owner thread");
        check(!scene->removeObject(object->assetId + 1000000) && scene->revision() == revision,
              "Missing removal changed world version");
        check(scene->removeObject(object->assetId) && scene->objects().empty() &&
                  scene->directionLights().empty() && scene->revision() > revision,
              "Object removal retained a stale light or world version");
        scene->addObject(object);
        scene->clearObjects();
        check(scene->objects().empty() && scene->directionLights().empty(), "Clear retained ghost lights");
        scene->addObject(object);
        object->removeComponent<DirectionLight>();
        check(scene->directionLights().empty(), "Component removal did not update the live light index");
        auto light = object->addComponent<DirectionLight>();
        check(scene->directionLights().size() == 1 && scene->directionLights()[0] == light,
              "Live component insertion did not update the light index");
        object->removeComponent<DirectionLight>();
        check(!light->hasOwner() && !object->getComponent<DirectionLight>(),
              "Removed component retained owner or typed index");
        bool rebound = false;
        auto another = std::make_shared<RenderScene>();
        try {
            another->addObject(object);
        } catch (const std::logic_error &) {
            rebound = true;
        }
        check(rebound && another->objects().empty(), "Published object was attached to two worlds");
    }
    {
        auto scene = std::make_shared<RenderScene>();
        auto decoded = std::async(std::launch::async, [] {
                           auto object = std::make_shared<GameObject>("detached worker");
                           auto transform = object->addComponent<Transform>();
                           auto filter = object->addComponent<MeshFilter>();
                           filter->addShape(SHAPE::PLANE);
                           auto mesh = filter->getMeshes()[0];
                           auto material = std::make_shared<Material>();
                           mesh->setMaterial(material);
                           object->sealForTransfer();
                           bool frozen = false;
                           try {
                               transform->setTRS({9, 9, 9}, {0, 0, 0}, {1, 1, 1});
                           } catch (const std::logic_error &) {
                               frozen = true;
                           }
                           if (!frozen)
                               throw std::runtime_error("Producer wrote a sealed object");
                           frozen = false;
                           try {
                               material->setOpacityFactor(.5f);
                           } catch (const std::logic_error &) {
                               frozen = true;
                           }
                           if (!frozen)
                               throw std::runtime_error("Producer wrote a sealed material");
                           return object;
                       }).get();
        auto unsafe = std::async(std::launch::async, [] {
                          return std::make_shared<GameObject>("unsealed worker");
                      }).get();
        bool refused = false;
        try {
            scene->addObject(unsafe);
        } catch (const std::logic_error &) {
            refused = true;
        }
        check(refused && scene->objects().empty(), "Unsealed foreign object bypassed ownership transfer");
        auto foreignComponent =
            std::async(std::launch::async, [] { return std::make_shared<Transform>(); }).get();
        auto localObject = std::make_shared<GameObject>("local object");
        refused = false;
        try {
            localObject->addComponent(foreignComponent);
        } catch (const std::logic_error &) {
            refused = true;
        }
        check(refused && !localObject->getComponent<Transform>(),
              "Foreign component bypassed the sealed-object transfer boundary");
        scene->addObject(decoded);
        auto adoptedMesh = decoded->getComponent<MeshFilter>()->getMeshes()[0];
        adoptedMesh->setGeometry(adoptedMesh->getVertices(), adoptedMesh->getIndices());
        adoptedMesh->getMaterial()->setOpacityFactor(.75f);
        {
            auto transferred=std::async(std::launch::async,[] {
                auto world=std::make_shared<RenderScene>();auto camera=std::make_shared<Camera>();
                camera->setClipPlanes(.2f,250.f);world->setCamera(camera);
                auto object=std::make_shared<GameObject>();auto filter=object->addComponent<MeshFilter>();
                filter->addShape(SHAPE::PLANE);auto material=std::make_shared<Material>();
                filter->getMeshes()[0]->setMaterial(material);world->addObject(object);
                world->sealForTransfer();return world;
            }).get();
            auto target=std::make_shared<RenderScene>();target->replaceWith(*transferred);
            target->mainCamera()->setExposure(1.2f);
            target->objects()[0]->getComponent<MeshFilter>()->getMeshes()[0]->getMaterial()->setRoughnessFactor(.4f);
            check(target->mainCamera()->getFar()==250.f,"Sealed camera did not transfer to main thread");
        }
        auto transform = decoded->getComponent<Transform>();
        transform->setTRS({1, 2, 3}, {0, 0, 0}, {1, 1, 1});
        const auto original = transform->getPosition();
        auto port = scene->commandPort();
        auto request =
            std::async(std::launch::async, [port, id = decoded->assetId, component = transform->assetId] {
                return port.post(engine::SetTransform{id, component, {4, 5, 6}, {0, 10, 0}, {2, 2, 2}});
            }).get();
        check(transform->getPosition() == original, "Background post mutated world before main-thread drain");
        check(scene->applyCommands(1) == 1 && request.result.get().status == engine::CommandStatus::Applied &&
                  transform->getPosition() == glm::vec3(4, 5, 6),
              "Value command failed to update main-thread transform");
        auto foreign = std::async(std::launch::async, [decoded, transform] {
            bool query = false, write = false, owner = false;
            try {
                decoded->getComponent<Transform>();
            } catch (const std::logic_error &) {
                query = true;
            }
            try {
                transform->setTRS({9, 9, 9}, {0, 0, 0}, {1, 1, 1});
            } catch (const std::logic_error &) {
                write = true;
            }
            try {
                transform->owner();
            } catch (const std::logic_error &) {
                owner = true;
            }
            return query && write && owner;
        });
        check(foreign.get(), "Former worker retained access after object ownership handoff");
        auto invalid = port.post(
            engine::SetTransform{decoded->assetId, transform->assetId, {0, 0, 0}, {0, 0, 0}, {0, 1, 1}});
        auto cancelled = port.post(engine::SetDeferred{decoded->assetId, false});
        cancelled.cancel();
        scene->applyCommands();
        check(invalid.result.get().status == engine::CommandStatus::Invalid &&
                  transform->getPosition() == glm::vec3(4, 5, 6) &&
                  cancelled.result.get().status == engine::CommandStatus::Cancelled && decoded->isDeferred(),
              "Invalid or cancelled command partially modified an object");
        auto removed = port.post(engine::RemoveComponent{decoded->assetId, transform->assetId});
        scene->applyCommands();
        check(removed.result.get().status == engine::CommandStatus::Applied && !transform->hasOwner(),
              "Queued component removal retained an owner");
        auto next = decoded->addComponent<Transform>();
        auto staleComponent = port.post(
            engine::SetTransform{decoded->assetId, transform->assetId, {7, 8, 9}, {0, 0, 0}, {1, 1, 1}});
        scene->applyCommands();
        check(staleComponent.result.get().status == engine::CommandStatus::MissingTarget &&
                  next->getPosition() == glm::vec3(0),
              "Old component ID modified a replacement component");
        auto pending = port.post(engine::SetDeferred{decoded->assetId, false});
        auto staging = std::make_shared<RenderScene>();
        auto replacement = std::make_shared<GameObject>("new world");
        staging->addObject(replacement);
        scene->replaceWith(*staging);
        check(pending.result.get().status == engine::CommandStatus::StaleWorld &&
                  port.post(engine::SetDeferred{replacement->assetId, false}).result.get().status ==
                      engine::CommandStatus::StaleWorld,
              "World replacement accepted pending or late old-generation commands");
        auto livePort = scene->commandPort();
        auto applied = livePort.post(engine::SetDeferred{replacement->assetId, false});
        scene->applyCommands();
        check(applied.result.get().status == engine::CommandStatus::Applied && !replacement->isDeferred() &&
                  scene->findObject(replacement->assetId) == replacement,
              "New world port or entity lookup lost its target");
        auto closing = livePort.post(engine::RemoveObject{replacement->assetId});
        scene.reset();
        check(closing.result.get().status == engine::CommandStatus::Closed &&
                  livePort.post(engine::RemoveObject{replacement->assetId}).result.get().status ==
                      engine::CommandStatus::Closed,
              "World destruction left command futures unresolved");
    }
    {
        Camera a,b;engine::InputFrame message;message.movement[0]=true;message.mouseMoved=true;message.mouseX=12;
        engine::FixedStepClock first,second;
        a.applyInput(message,0,true);b.applyInput(message,0,true);
        first.advance(.1,[&](double dt){a.applyInput(message,float(dt),false);});
        for(int i=0;i<10;++i)second.advance(.01,[&](double dt){b.applyInput(message,float(dt),false);});
        check(glm::length(a.getPosition()-b.getPosition())<1e-6f && a.getYaw()==b.getYaw(),"Input replay depends on render partition or repeats pointer deltas");
    }
    {
        Material material;
        Material copy = material;
        check(material.assetId != copy.assetId, "Copied live material reused another asset identity");
        auto object = std::make_shared<GameObject>("ownership regression");
        auto terrain = std::make_shared<TerrainComponent>();
        object->addComponent(terrain);
        check(object->addComponent<TerrainComponent>() == terrain,
              "Template component insertion returned an unattached duplicate");
        check(object->name == "ownership regression" && terrain->owner() == object,
              "Object name or component owner lost");
        auto duplicate = std::make_shared<TerrainComponent>();
        object->addComponent(duplicate);
        check(!duplicate->hasOwner(), "Rejected duplicate component retained an owner");
        std::weak_ptr<GameObject> weakObject = object;
        object.reset();
        check(weakObject.expired() && !terrain->hasOwner(),
              "Component / object ownership cycle leaked scene");
        bool rejected = false;
        try {
            terrain->owner();
        } catch (const std::logic_error &) {
            rejected = true;
        }
        check(rejected, "Expired component owner dereferenced");
    }
    {
        auto scene = std::make_shared<RenderScene>();
        auto base = std::filesystem::temp_directory_path() /
                    ("scene-renderer-loader-validation-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(base);
        {
            std::ofstream image(base / "shared.ppm", std::ios::binary);
            image << "P6\n1 1\n255\n";
            const char rgb[3] = {32, 64, 96};
            image.write(rgb, 3);
        }
        {
            std::vector<std::future<std::shared_ptr<Texture>>> requests;
            for (int i = 0; i < 8; ++i)
                requests.push_back(std::async(std::launch::async, [base, i] {
                    return ResourceManager::GetInstance()->getResourceAsync(
                        (i % 2 ? base / "." / "shared.ppm" : base / "shared.ppm").string());
                }));
            auto first = requests[0].get();
            for (size_t i = 1; i < requests.size(); ++i)
                check(requests[i].get() == first,
                      "Normalized paths were decoded into different Texture assets");
            check(first->getWidth() == 1 && first->getHeight() == 1 && first->pixels() &&
                      ResourceManager::GetInstance()->find((base / "." / "shared.ppm").string()) == first,
                  "CPU texture cache returned invalid data or inconsistent lookup");
            first.reset();
            check(ResourceManager::GetInstance()->releaseUnused() > 0 &&
                      !ResourceManager::GetInstance()->find((base / "shared.ppm").string()),
                  "Unused real Texture cache could not be reclaimed");
        }
        {
            std::ofstream file(base / "empty.json");
            file << "{}";
            std::ofstream bad(base / "bad.json");
            bad << "{";
            std::ofstream terrain(base / "terrain.json");
            terrain << "{";
            std::ofstream worker(base / "worker.json");
            worker << "{\"terrain\":\"" << (base / "terrain.json").string() << "\"}";
            std::ofstream object(base / "object.json");
            json objectData = json::parse(
                R"({"name":"worker object","components":{"Transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"MeshFilter":{"shape":"plane","material":{"albedoFactor":[0.2,0.3,0.4]}}}})");
            objectData["components"]["MeshFilter"]["material"]["textures"]["material.albedo"] =
                (base / "shared.ppm").string();
            object << objectData.dump();
            std::ofstream valid(base / "valid.json");
            valid << "{\"objects\":{\"one\":\"" << (base / "object.json").string() << "\"}}";
        }
        auto loader = Loader::GetInstance();
        loader->loadSceneAsync(scene, (base / "empty.json").string());
        loader->loadSceneAsync(scene, (base / "empty.json").string());
        auto sentinel = std::make_shared<GameObject>("keep on failure");
        scene->addObject(sentinel);
        scene->setCamera(std::make_shared<Camera>());
        auto camera = scene->mainCamera();
        uint64_t revision = scene->revision();
        bool rejected = false;
        try {
            loader->loadSceneAsync(scene, (base / "bad.json").string());
        } catch (const std::exception &error) {
            check(std::string(error.what()).find("parse_error") != std::string::npos,
                  "Loader exception lost its JSON parse diagnostic");
            rejected = true;
        }
        check(rejected && scene->revision() == revision, "Loader cleared scene before validating JSON");
        rejected = false;
        try {
            loader->loadSceneAsync(scene, (base / "worker.json").string());
        } catch (const std::exception &error) {
            check(std::string(error.what()).find("parse_error") != std::string::npos,
                  "Loader exception lost its JSON parse diagnostic");
            rejected = true;
        }
        check(rejected && scene->revision() == revision,
              "Loader lost worker exception or published a partial scene");
        check(scene->objects().size() == 1 && scene->objects()[0] == sentinel &&
                  scene->mainCamera() == camera,
              "Failed staging build modified the live scene");
        {
            std::ofstream missing(base / "missing.json");
            missing << "{\"objects\":{\"bad\":\"" << (base / "absent.json").string() << "\"}}";
        }
        rejected = false;
        try {
            loader->loadSceneAsync(scene, (base / "missing.json").string());
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        check(rejected && scene->objects().at(0) == sentinel && scene->revision() == revision,
              "Missing child file was silently accepted or cleared the world");
        if (device->backend() != rhi::Backend::OpenGL) {
            auto request = loader->buildScene((base / "empty.json").string());
            auto built = request.result.get();
            check(scene->objects().at(0) == sentinel && scene->revision() == revision,
                  "Async build published before logic-thread commit");
            scene->replaceWith(*built);
            check(scene->objects().empty() && scene->mainCamera() == camera && scene->revision() > revision,
                  "Successful commit lost camera or failed to replace world");
            // Both scene children and textures resolve relative to their own document.
            {
                std::ofstream relativeObject(base / "relative-object.json");
                relativeObject << R"({"name":"relative object","components":{"MeshFilter":{"shape":"plane","material":{"textures":{"material.albedo":"shared.ppm"}}}}})";
                std::ofstream relativeScene(base / "relative-scene.json");
                relativeScene << R"({"objects":{"one":"relative-object.json"}})";
            }
            auto rootBefore=engine::AssetPath::root();
            engine::AssetPath::setRoot(base);
            auto relative=loader->buildScene("relative-scene.json");
            auto relativeWorld=relative.result.get();
            engine::AssetPath::setRoot(rootBefore);
            scene->replaceWith(*relativeWorld);
            auto relativeTexture=scene->objects().at(0)->getComponent<MeshFilter>()->getMeshes().at(0)->getMaterial()->getTextures().at("material.albedo");
            check(relativeTexture->getWidth()==1 && std::filesystem::path(relativeTexture->getPath()).is_absolute(),
                  "Asset root or document-relative texture path was ignored");
            auto populated = loader->buildScene((base / "valid.json").string());
            auto builtObject = populated.result.get();
            scene->replaceWith(*builtObject);
            auto object = scene->objects().at(0);
            auto loadedMaterial = object->getComponent<MeshFilter>()->getMeshes().at(0)->getMaterial();
            check(loadedMaterial && loadedMaterial->getAlbedoFactor() == glm::vec3(.2f, .3f, .4f) &&
                      loadedMaterial->getTextures().count("material.albedo") == 1,
                  "Basic shape lost its JSON material during worker/main handoff");
            auto transform = object->getComponent<Transform>();
            transform->setTRS({1, 2, 3}, {0, 0, 0}, {1, 1, 1});
            check(transform->owner() == object && scene->findObject(object->assetId) == object,
                  "Decoder/coordinator/main ownership handoff lost component or entity index");
        }
        loader->loadSceneAsync(scene, (base / "empty.json").string());
        loader->waitIdle();
        std::filesystem::remove_all(base);
    }
    {
        using namespace rhi;
        {
            engine::RenderGraph graph;for(auto name:{"first","second","overlap"})graph.describe(name,{1,1,true,"rgba8"});
            graph.add("a",{{"first",engine::RenderGraph::Access::Write},{"overlap",engine::RenderGraph::Access::Write}},[]{});
            graph.add("b",{{"first",engine::RenderGraph::Access::Read}},[]{});
            graph.add("c",{{"second",engine::RenderGraph::Access::Write},{"overlap",engine::RenderGraph::Access::Read}},[]{});
            const TextureDesc desc{16,16,Format::RGBA8UNorm,TextureUsage::ColorAttachment|TextureUsage::Sampled,"Transient validation"};
            auto before=device->resourceMemory().textureBytes;
            GraphTextures pool(device,graph.compile(),{{"first",desc},{"second",desc},{"overlap",desc}});
            check(pool.texture("first").value==pool.texture("second").value && pool.texture("first").value!=pool.texture("overlap").value && device->resourceMemory().textureBytes-before==2*16*16*4,
                  "Transient allocator ignored lifetimes or reused overlapping texture storage");
        }
        if(device->supportsTextureSubresources()) {
            Resources levels(device);auto before=device->resourceMemory().textureBytes;
            auto texture=levels.texture({8,8,Format::RGBA8UNorm,TextureUsage::Sampled|TextureUsage::ColorAttachment|TextureUsage::CopyDestination|TextureUsage::CopySource,"Mip/layer validation",4,2});
            check(device->resourceMemory().textureBytes-before==(64+16+4+1)*4*2,"Texture quota omitted mip or array layers");
            auto base=device->createTextureView({texture,{0,1,0,1}}),small=device->createTextureView({texture,{2,1,1,1}});
            std::vector<uint8_t> red(64*4,0),green(4*4,0);for(size_t i=0;i<red.size();i+=4){red[i]=255;red[i+3]=255;}for(size_t i=0;i<green.size();i+=4){green[i+1]=255;green[i+3]=255;}
            device->writeTextureRegion(texture,{0,0,8,8,0,0},red.data(),red.size());
            device->writeTextureRegion(texture,{0,0,2,2,2,1},green.data(),green.size());
            check(device->readTextureSubresource(texture,{0,0})==red && device->readTextureSubresource(texture,{2,1})==green,"Subresource upload/readback changed sibling mip or layer");
            auto commands=device->createCommandList();RenderPassDesc pass;pass.color=small;pass.clearColor={0,0,1,1};commands.beginRenderPass(pass);commands.setPassLabel("Mip 2 layer 1 clear");commands.endRenderPass();device->submit(commands);
            auto blue=green;for(size_t i=0;i<blue.size();i+=4){blue[i+1]=0;blue[i+2]=255;}
            check(device->readTextureSubresource(texture,{2,1})==blue && device->readTextureSubresource(texture,{0,0})==red,"Subresource attachment used base level or another layer");
            auto destination=levels.texture({2,2,Format::RGBA8UNorm,TextureUsage::CopySource|TextureUsage::CopyDestination,"Subresource copy"});
            auto copy=device->createCommandList();copy.copyTexture(texture,destination,{2,1});device->submit(copy);
            check(device->readTexture(destination)==blue,"Subresource copy failed extent or level selection");
            bool denied=false;try{device->createTextureView({texture,{4,1,0,1}});}catch(const std::invalid_argument&){denied=true;}check(denied,"Invalid mip view accepted");
            denied=false;try{device->writeTextureRegion(texture,{0,0,3,2,2,1},green.data(),green.size());}catch(const std::invalid_argument&){denied=true;}check(denied,"Subresource upload exceeds selected mip");
            device->destroyTextureView(base);device->destroyTextureView(small);
        } else {
            bool denied=false;try{device->createTexture({4,4,Format::RGBA8UNorm,TextureUsage::Sampled,"Unsupported mip storage",2,1});}catch(const std::invalid_argument&){denied=true;}
            check(denied,"Backend silently flattened unsupported mip storage");
        }
    }
    {
        auto cache=GpuImageCache::forDevice(device);
        auto pixels=std::make_shared<ImageRGBA8>();pixels->width=pixels->height=128;pixels->pixels.resize(128*128*4);
        for(size_t i=0;i<pixels->pixels.size();++i)pixels->pixels[i]=uint8_t((i*19+11)%251);
        auto image=cache->acquire(pixels,true);check(image && !image->ready(),"Deferred image was eagerly uploaded");
        check(cache->upload(image,16384)==16384 && !image->ready(),"Image exceeded its row upload budget");
        check(cache->upload(image,SIZE_MAX)==49152 && image->ready() && device->readTexture(image->texture())==pixels->pixels,
              "Chunked image upload changed row layout or final pixels");
        if(device->computeLimits().maxStorageImages && device->backend()!=rhi::Backend::OpenGL) {
            GpuVirtualTexture vt(device,heightVirtualSource(256,256,std::vector<float>(256*256)),4);vt.enableAsync();
            Resources feedbackFrame(device);
            auto depth=feedbackFrame.texture({64,64,rhi::Format::RGBA32Float,rhi::TextureUsage::Sampled|rhi::TextureUsage::CopyDestination,"Feedback validation depth"});
            std::vector<float> values(64*64*4,.5f);device->writeTextureFloat(depth,values.data(),values.size()*4);
            auto view=feedbackFrame.view(depth);glm::mat4 inverseVP=glm::scale(glm::mat4(1),glm::vec3(.2f,.2f,1));
            auto model=glm::rotate(glm::mat4(1),glm::radians(90.f),glm::vec3(1,0,0));
            vt.recordFeedback(view,inverseVP,model,64,64,-2,2);device->waitIdle();
            vt.prepare(glm::mat4(1),model,64,64);
            check(vt.feedbackSamples()==4096 && vt.feedbackPageCount()>0 && vt.residentPages()<=vt.capacity(),
                  "GPU depth feedback failed readback, decode or physical cache bound");
        }
    }
    {
        auto scene = makeForwardDemoScene();
        auto seedTexture = std::make_shared<Texture>();
        seedTexture->setPixels(4,1,4,std::vector<unsigned char>(16,128));
        scene->objects().at(0)->getComponent<MeshFilter>()->getMeshes().at(0)->getMaterial()->addTexture(
            seedTexture, "material.albedo");
        SceneSnapshotBuilder builder;
        auto first = builder.capture(scene, 0, 64, 64);
        check(!first->draws.empty(), "Snapshot lost scene draws");
        {
            // Warm three independent records, then replace all three meshes under
            // a two-asset budget. The deferred third draw must retain its material.
            SceneAdapter streaming(device);
            RenderWorldSnapshot seed = *first;
            seed.draws.clear();
            seed.terrain.reset();
            for (uint64_t i = 0; i < 3; ++i) {
                auto draw = first->draws[0];
                draw.subdivision = false;
                draw.objectId += 100000 + i;
                auto mesh = std::make_shared<MeshPayload>(*draw.mesh);
                mesh->id += 100000 + i;
                draw.mesh = mesh;
                auto material = std::make_shared<MaterialPayload>(*draw.material);
                material->id += 100000 + i;
                draw.material = material;
                seed.draws.push_back(draw);
            }
            auto warm = streaming.resolve(seed);
            auto pending = seed;
            pending.asynchronousStreaming = true;
            for (auto &draw : pending.draws) {
                auto mesh = std::make_shared<MeshPayload>(*draw.mesh);
                ++mesh->revision;
                draw.mesh = mesh;
            }
            auto firstUpload = streaming.resolve(pending), secondUpload = streaming.resolve(pending);
            check(firstUpload.packets.size() == 2 && secondUpload.packets.size() == 3 &&
                      secondUpload.assetUploads == 1 &&
                      secondUpload.packets[2].material == warm.packets[2].material,
                  "Upload backpressure evicted a still-referenced material or blocked progress");
        }
        {
            SceneAdapter transactional(device);
            auto seed = *first;
            seed.terrain.reset();
            seed.draws.resize(1);
            seed.draws[0].subdivision = false;
            auto committed = transactional.resolve(seed);
            auto changed = seed;
            auto geometry = std::make_shared<MeshPayload>(*seed.draws[0].mesh);
            ++geometry->revision;
            geometry->vertices[0].position.x += .1f;
            changed.draws[0].mesh = geometry;
            changed.draws[0].parameters.albedoAlpha.w = .5f;
            auto extra = changed.draws[0];
            auto extraMesh = std::make_shared<MeshPayload>(*geometry);
            extraMesh->id += 1000000;
            extra.mesh = extraMesh;
            changed.draws.push_back(extra);
            auto images = GpuImageCache::forDevice(device);
            images->releaseIdle();
            device->waitIdle();
            const auto before = device->resourceMemory();
            // First mesh and its new parameter buffers fit. The later mesh fails.
            device->setResourceBudget(before.usedBytes() + geometry->vertices.size() * sizeof(MeshVertex) +
                                      geometry->indices.size() * 4 + 96);
            bool rejected = false;
            try {
                transactional.resolve(changed);
            } catch (const rhi::ResourceBudgetExceeded &) {
                rejected = true;
            }
            device->waitIdle();
            check(rejected && device->resourceMemory().usedBytes() == before.usedBytes(),
                  "Late scene allocation did not reject or leaked candidate resources");
            device->setResourceBudget(before.budgetBytes);
            auto retained = transactional.resolve(seed);
            check(retained.packets[0].mesh == committed.packets[0].mesh &&
                      retained.packets[0].material == committed.packets[0].material &&
                      !committed.packets[0].material->transparent() &&
                      retained.frame.historyKey == committed.frame.historyKey,
                  "Rejected scene changed committed mesh/material/epoch or partially updated parameters");
            // Runtime may reject after resolve, so explicit publication must also restore caches.
            transactional.beginPublication();
            auto tentative = transactional.resolve(changed);
            check(tentative.packets[0].mesh != committed.packets[0].mesh,
                  "Publication did not stage changed geometry");
            tentative.packets.clear();
            transactional.rollbackPublication();
            auto restored = transactional.resolve(seed);
            check(restored.packets[0].mesh == committed.packets[0].mesh &&
                      restored.packets[0].material == committed.packets[0].material,
                  "External publication rollback lost committed records");
        }
        {
            auto geometry=std::make_shared<MeshPayload>();
            geometry->id=engine::nextIdentity();geometry->revision=1;
            geometry->vertices={{{-1,0,0},{0,1,0},{0,0}},{{1,0,0},{0,1,0},{1,0}},{{0,0,1},{0,1,0},{.5f,1}}};
            geometry->indices={0,1,2};
            RenderWorldSnapshot seed=*first;seed.draws.clear();seed.terrain.reset();seed.asynchronousStreaming=true;
            SnapshotDraw draw;draw.mesh=geometry;draw.objectId=engine::nextIdentity();seed.draws.push_back(draw);
            SceneAdapter streaming(device);
            streaming.setMeshUploadBudget({64,32,1000});
            auto deferred=streaming.resolve(seed);
            check(deferred.packets.empty() && deferred.meshUploadsPending==1 && deferred.meshUploadBytes==64,
                  "Incomplete mesh became drawable or exceeded per-frame upload bytes");
            auto finished=streaming.resolve(seed);
            check(finished.packets.size()==1 && finished.meshUploadsPending==0 && finished.meshUploadBytes==44,
                  "Mesh did not finish remaining vertices and indices in separate chunks");
            auto published=finished.packets[0].mesh;
            std::vector<MeshVertex> vertices(3);std::vector<uint32_t> indices(3);
            device->readBuffer(published->vertexBuffer(),0,vertices.size()*sizeof(MeshVertex),vertices.data());
            device->readBuffer(published->indexBuffer(),0,indices.size()*4,indices.data());
            check(std::memcmp(vertices.data(),geometry->vertices.data(),vertices.size()*sizeof(MeshVertex))==0 && indices==geometry->indices,
                  "Chunked mesh bytes differ from immutable CPU payload");
            auto changed=std::make_shared<MeshPayload>(*geometry);++changed->revision;
            changed->vertices[0].position.y=2;seed.draws[0].mesh=changed;
            streaming.beginPublication();
            auto tentative=streaming.resolve(seed);
            check(tentative.packets[0].mesh==published && tentative.meshUploadsPending==1,
                  "Revision upload replaced the prior mesh before completion");
            tentative.packets.clear();streaming.rollbackPublication();
            auto replayed=streaming.resolve(seed);
            check(replayed.meshUploadBytes==64 && replayed.packets[0].mesh==published,
                  "Rollback lost or prematurely committed upload progress");
            // Cancel a revision by reverting to the published source.
            seed.draws[0].mesh=geometry;
            auto reverted=streaming.resolve(seed);
            check(reverted.meshUploadsPending==0 && reverted.meshUploadBytes==0 && reverted.packets[0].mesh==published,
                  "Reverted source retained a stale pending upload");
            seed.draws[0].mesh=changed;
            streaming.resolve(seed);
            auto replacement=streaming.resolve(seed);
            check(replacement.packets[0].mesh!=published && replacement.packets[0].mesh->boundsMax().y==2 &&
                      replacement.frame.historyKey!=finished.frame.historyKey,
                  "Completed revision did not replace geometry/bounds or invalidate temporal history");
            // A second source supersedes an in-progress revision without publishing its old bytes.
            auto superseded=std::make_shared<MeshPayload>(*changed);++superseded->revision;
            superseded->vertices[1].position.y=3;seed.draws[0].mesh=superseded;
            streaming.resolve(seed);
            auto latest=std::make_shared<MeshPayload>(*superseded);++latest->revision;
            latest->vertices[1].position.y=4;seed.draws[0].mesh=latest;
            streaming.resolve(seed);
            auto newest=streaming.resolve(seed);
            check(newest.packets[0].mesh->boundsMax().y==4,"Superseded upload published stale geometry");
            {
                auto large=std::make_shared<MeshPayload>();large->id=engine::nextIdentity();large->revision=1;
                large->vertices.assign(300000,{{.25f,.5f,.75f},{0,1,0},{0,0}});
                large->indices={0,1,2};
                auto big=seed;big.draws[0].mesh=large;
                SceneAdapter bounded(device);bounded.setMeshUploadBudget({8*1024*1024,256*1024,1000});
                auto partial=bounded.resolve(big);
                check(partial.packets.empty() && partial.meshUploadBytes==8*1024*1024 &&
                          partial.meshUploadChunks==32 && partial.meshUploadsPending==1,
                      "Large mesh bypassed hard byte/chunk upload limits");
                auto complete=bounded.resolve(big);
                check(complete.packets.size()==1 && complete.meshUploadsPending==0 &&
                          partial.meshUploadBytes+complete.meshUploadBytes==large->vertices.size()*sizeof(MeshVertex)+12,
                      "Large mesh upload did not resume precisely at its next range");
                std::vector<MeshVertex> readback(large->vertices.size());
                device->readBuffer(complete.packets[0].mesh->vertexBuffer(),0,readback.size()*sizeof(MeshVertex),readback.data());
                check(std::memcmp(readback.data(),large->vertices.data(),readback.size()*sizeof(MeshVertex))==0,
                      "Large mesh chunk boundary corrupted GPU bytes");
            }
            {
                auto limited=seed;limited.draws[0].mesh=geometry;
                auto another=std::make_shared<MeshPayload>(*geometry);another->id=engine::nextIdentity();
                auto second=limited.draws[0];second.mesh=another;limited.draws.push_back(second);
                SceneAdapter bounded(device);bounded.setMeshUploadBudget({64,32,1000,1});
                device->waitIdle();const auto before=device->resourceMemory().usedBytes();
                auto partial=bounded.resolve(limited);
                check(partial.packets.empty() && partial.meshUploadsPending==2 && partial.assetUploads==1 &&
                          device->resourceMemory().usedBytes()==before+108,
                      "Pending mesh capacity allocated more than one job or admitted an incomplete draw");
                auto next=bounded.resolve(limited);auto later=bounded.resolve(limited);auto complete=bounded.resolve(limited);
                check(next.packets.size()==1 && later.packets.size()==1 && complete.packets.size()==2 &&
                          complete.meshUploadsPending==0,
                      "Pending job backpressure starved its successor");
                // Even a vanishingly small time budget must make one bounded chunk of progress.
                auto revision=std::make_shared<MeshPayload>(*geometry);++revision->revision;
                limited.draws.resize(1);limited.draws[0].mesh=revision;
                bounded.setMeshUploadBudget({64,32,1e-12,1});
                auto timed=bounded.resolve(limited);
                check(timed.meshUploadChunks==1 && timed.meshUploadBytes==32 && timed.meshUploadsPending==1,
                      "CPU time budget failed to stop after one chunk or caused permanent starvation");
            }
            auto pending=GpuMesh::beginUpload(device,3,3);
            auto commands=device->createCommandList();bool denied=false;
            try {pending->draw(commands);}catch(const std::logic_error&){denied=true;}
            check(denied && !pending->ready(),"Pending GPU mesh allowed drawing uninitialized memory");
            denied=false;
            try {pending->uploadVertices(1,geometry->vertices.data()+1,1);}catch(const std::invalid_argument&){denied=true;}
            check(denied,"Mesh upload accepted a hole in its initialized prefix");
            // Identical renderer instances share native pipelines; independent handles remain valid.
            auto a=std::make_unique<ForwardPbrRenderer>(device,rhi::defaultShaderDirectory(),64,64,PbrPath::Forward);
            const auto stats=device->pipelineCacheStats();
            auto b=std::make_unique<ForwardPbrRenderer>(device,rhi::defaultShaderDirectory(),64,64,PbrPath::Forward);
            check(device->pipelineCacheStats().graphicsBuilds==stats.graphicsBuilds && device->pipelineCacheStats().hits>stats.hits,
                  "Identical native renderer recompiled pipelines");
            auto frame=finished.frame;frame.taa=false;frame.sky=false;frame.shadows=false;
            a->render(frame,finished.packets);auto expected=device->readTexture(a->output());
            a.reset();b->render(frame,finished.packets);
            check(device->readTexture(b->output())==expected,"Releasing one renderer invalidated another pipeline lease");
        }
        auto unchanged = builder.capture(scene, 1, 64, 64);
        check(first->draws[0].mesh == unchanged->draws[0].mesh, "Unchanged geometry was recopied each frame");
        std::shared_ptr<GameObject> object;
        for (const auto &candidate : scene->objects())
            if (candidate->assetId == first->draws[0].objectId)
                object = candidate;
        check(bool(object), "Snapshot object identity is not stable");
        auto filter = object->getComponent<MeshFilter>();
        auto transform = object->getComponent<Transform>();
        auto mesh = filter->getMeshes()[0];
        auto oldPosition = first->draws[0].mesh->vertices[0].position;
        transform->setPosition(transform->getPosition() + glm::vec3(10, 0, 0));
        auto vertices = mesh->getVertices();
        for (auto &vertex : vertices)
            vertex.Position.x += 1;
        mesh->setGeometry(std::move(vertices), mesh->getIndices());
        auto updated = builder.capture(scene, 2, 64, 64);
        check(first->draws[0].mesh->vertices[0].position == oldPosition &&
                  first->draws[0].model != updated->draws[0].model,
              "Published snapshot aliases mutable scene state");
        check(first->draws[0].mesh != updated->draws[0].mesh &&
                  first->draws[0].mesh->id == updated->draws[0].mesh->id,
              "Asset revision did not preserve ID and rebuild immutable payload");
        auto material = mesh->getMaterial();
        material->setRoughnessFactor(.27f);
        auto scalarEdit = builder.capture(scene, 3, 64, 64);
        check(scalarEdit->draws[0].material == updated->draws[0].material &&
                  std::abs(scalarEdit->draws[0].parameters.factors.y - .27f) < 1e-6f,
              "Material scalar edit failed or unnecessarily rebuilt image payload");
        auto replacement = std::make_shared<Texture>();
        replacement->setPixels(1,1,4,{7,11,19,255});
        material->addTexture(replacement, "material.albedo");
        auto imageEdit = builder.capture(scene, 4, 64, 64);
        check(imageEdit->draws[0].material != scalarEdit->draws[0].material &&
                  imageEdit->draws[0].material->id == scalarEdit->draws[0].material->id &&
                  imageEdit->draws[0].material->images[0] &&
                  scalarEdit->draws[0].material->images[0] &&
                  imageEdit->draws[0].material->images[0]->pixels[0] == 7 &&
                  scalarEdit->draws[0].material->images[0]->width == 4,
              "Texture replacement failed to rebuild immutable payload or modified earlier frame");
        replacement->setPixels(1,1,4,{23,29,31,255});
        auto pixelEdit=builder.capture(scene,5,64,64);
        check(pixelEdit->draws[0].material!=imageEdit->draws[0].material &&
              pixelEdit->draws[0].material->images[0]->pixels[0]==23 && imageEdit->draws[0].material->images[0]->pixels[0]==7,
              "In-place Texture replacement missed material revision or aliased old snapshot");
        auto pixelRevision=replacement->revision();bool pixelRejected=false;
        try{replacement->setPixels(2,2,4,{1});}catch(const std::invalid_argument&){pixelRejected=true;}
        check(pixelRejected && replacement->revision()==pixelRevision && replacement->pixels()[0]==23,"Invalid Texture partially committed");
        auto textureDenied=std::async(std::launch::async,[&]{try{replacement->snapshot();return false;}catch(const std::logic_error&){return true;}});
        check(textureDenied.get(),"Mutable Texture admitted foreign-thread access");
        auto frozen=std::make_shared<Texture>();frozen->setPixels(1,1,4,{3,5,7,255});frozen->freeze();
        auto frozenRead=std::async(std::launch::async,[frozen]{return frozen->snapshot().data()[0]==3;});
        check(frozenRead.get(),"Immutable decoded Texture cannot cross threads");
        pixelRejected=false;try{frozen->setPixels(1,1,4,{0,0,0,0});}catch(const std::logic_error&){pixelRejected=true;}
        check(pixelRejected && frozen->pixels()[0]==3,"Decoded cache Texture remained mutable");
        auto foreign = std::async(std::launch::async, [&] {
            try {
                scene->addObject(std::make_shared<GameObject>());
            } catch (const std::logic_error &) {
                return true;
            }
            return false;
        });
        check(foreign.get(), "Mutable world accepted a foreign-thread write");
        auto gpuWrong = std::async(std::launch::async, [&] {
            try {
                device->createTexture(
                    {1, 1, rhi::Format::RGBA8UNorm, rhi::TextureUsage::Sampled, "wrong thread"});
            } catch (const std::logic_error &) {
                return true;
            }
            return false;
        });
        check(gpuWrong.get(), "RHI did not reject foreign-thread texture creation");
        scene->destroy();
        SceneAdapter adapter(device);
        auto detached = adapter.resolve(*first);
        check(detached.packets.size() == first->draws.size(),
              "Detached snapshot could not render after world destruction");
        if (device->backend() != rhi::Backend::OpenGL) {
            // Old mutable scene is already destroyed. Render only detached data,
            // including a GUI packet whose original ImGui buffers are overwritten.
            ImGui::CreateContext();
            auto &io = ImGui::GetIO();
            io.DisplaySize = {64, 64};
            io.DeltaTime = 1.f / 60;
            io.IniFilename = nullptr;
            auto gui = std::make_unique<GuiRenderer>(device, rhi::defaultShaderDirectory());
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({64, 64});
            ImGui::Begin("Snapshot GUI");
            ImGui::TextUnformatted("old frame");
            ImGui::End();
            ImGui::Render();
            auto ui = GuiFrame::capture(ImGui::GetDrawData());
            size_t oldVertices = 0;
            for (const auto &list : ui.lists)
                oldVertices += list.vertices.size();
            ImGui::NewFrame();
            ImGui::Begin("New GUI");
            ImGui::TextUnformatted("new frame changed");
            ImGui::End();
            ImGui::Render();
            size_t copiedVertices = 0;
            for (const auto &list : ui.lists)
                copiedVertices += list.vertices.size();
            check(oldVertices == copiedVertices && oldVertices > 0,
                  "GUI packet aliases reused ImGui buffers");
            {
                engine::RenderRuntime runtime(device, std::move(gui), 1);
                runtime.submit({first, std::move(ui), {}});
                runtime.submit({first, {}, {}});
                runtime.finish();
                check(runtime.framesRendered() == 2 && runtime.peakResourceBytes() > 0,
                      "Render worker lost packets or resource accounting");
                check(runtime.renderP95Milliseconds() > 0 &&
                          runtime.renderP99Milliseconds() >= runtime.renderP95Milliseconds() &&
                          runtime.imageUploads() > 0,
                      "Render worker omitted frame percentiles or GPU image cache statistics");
            }
            io.Fonts->SetTexID(nullptr);
            io.BackendRendererName = nullptr;
            io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
            ImGui::DestroyContext();
            {
                engine::RenderRuntime runtime(device,{},1);runtime.submit({first,{},{}});
                auto pump=[&](engine::ScenePreparationTicket& ticket) {
                    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                    while(ticket.ready.wait_for(std::chrono::seconds(0))!=std::future_status::ready && std::chrono::steady_clock::now()<deadline)
                        runtime.submit({first,{},{}});
                    check(ticket.ready.wait_for(std::chrono::seconds(0))==std::future_status::ready,"GPU scene preparation stalled");
                };
                auto ticket=runtime.prepareScene(first);pump(ticket);ticket.ready.get();
                runtime.activatePrepared(ticket.token);runtime.submit({first,{},{}});
                auto cancelled=std::make_shared<std::atomic<bool>>(true);
                auto cancelTicket=runtime.prepareScene(first,cancelled);pump(cancelTicket);
                bool denied=false;try{cancelTicket.ready.get();}catch(const std::runtime_error&){denied=true;}
                check(denied,"Cancelled GPU preparation was acknowledged");
                auto invalid=std::make_shared<RenderWorldSnapshot>(*first);
                invalid->frame.viewportWidth=0;
                auto rejected=runtime.prepareScene(invalid);pump(rejected);
                denied=false;try{rejected.ready.get();}catch(const std::exception&){denied=true;}
                check(denied,"Invalid candidate silently published");
                auto retry=runtime.prepareScene(first);pump(retry);retry.ready.get();
                runtime.activatePrepared(retry.token);runtime.submit({first,{},{}});runtime.finish();
                check(runtime.framesRendered()>2,"Preparation stopped the old world's rendering or recovery");
            }
            {
                engine::RenderRuntime runtime(device, {}, 1);
                runtime.notifySurfaceExtent(0, 0);
                runtime.submit({first, {}, {}});
                runtime.finish();
                check(runtime.framesRendered() == 1, "Minimized surface failed to drain its queued frame");
            }
            {
                engine::RenderRuntime runtime(device, {}, 1);
                runtime.notifySurfaceExtent(64, 64);
                runtime.submit({first, {}, {}});
                runtime.finish();
                check(runtime.framesRendered() == 1, "Surface restoration did not resume rendering");
            }
            {
                const auto directory = std::filesystem::temp_directory_path() /
                                       ("scene-publication-validation-" +
                                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                std::filesystem::create_directories(directory);
                const auto before = device->resourceMemory();
                const size_t budget = before.usedBytes() + before.peakBytes + 16384;
                const auto extent = device->graphicsLimits().maxTextureDimension2D;
                check(uint64_t(extent) * extent * 4 > budget,
                      "Publication validation requires a resize larger than the warm resource quota");
                device->setResourceBudget(budget);
                {
                    engine::RenderRuntime runtime(device, {}, 1);
                    runtime.notifySurfaceExtent(64, 64);
                    auto awaitFrames = [&](uint64_t count) {
                        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                        while (runtime.framesRendered() < count && std::chrono::steady_clock::now() < deadline) {
                            runtime.rethrowFailure();
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        check(runtime.framesRendered() >= count, "Publication rollback stopped the render queue");
                    };
                    runtime.submit({first, {}, (directory / "first.ppm").string()});
                    awaitFrames(1);
                    auto resize = std::make_shared<RenderWorldSnapshot>(*first);
                    resize->frame.viewportWidth = resize->frame.viewportHeight = extent;
                    runtime.submit({resize, {}, (directory / "resize-rejected.ppm").string()});
                    awaitFrames(2);
                    std::this_thread::sleep_for(std::chrono::milliseconds(260));
                    auto ocean = std::make_shared<RenderWorldSnapshot>(*first);
                    OceanSurfaceSettings largeOcean;
                    largeOcean.spectrum.size = 2048;
                    ocean->frame.oceans.push_back(largeOcean);
                    runtime.submit({ocean, {}, (directory / "ocean-rejected.ppm").string()});
                    awaitFrames(3);
                    std::this_thread::sleep_for(std::chrono::milliseconds(260));
                    auto restored = std::make_shared<RenderWorldSnapshot>(*first);
                    restored->exposure = 0;
                    runtime.submit({restored, {}, (directory / "restored.ppm").string()});
                    runtime.finish();
                    check(runtime.framesRendered() == 4 && runtime.rejectedPublications() == 2 &&
                              runtime.fallbackFrames() == 2 && runtime.memoryPressureEvents() >= 2 &&
                              !runtime.lastRecoveryMessage().empty(),
                          "Runtime did not reject two candidates, retain prior output and resume");
                }
                device->setResourceBudget(before.budgetBytes);
                auto bytes = [&](const char *name) {
                    std::ifstream input(directory / name, std::ios::binary);
                    check(bool(input), "Publication screenshot was not written");
                    return std::vector<char>(std::istreambuf_iterator<char>(input), {});
                };
                const auto pixels = bytes("first.ppm");
                check(pixels == bytes("resize-rejected.ppm") && pixels == bytes("ocean-rejected.ppm") &&
                          pixels != bytes("restored.ppm"),
                      "Failed publication overwrote prior pixels/extent or valid scene did not resume");
                std::filesystem::remove_all(directory);
            }
            {
                const auto before=device->resourceMemory();device->setResourceBudget(before.usedBytes()+128*1024*1024);
                {
                    engine::RenderRuntime runtime(device,{},1);runtime.notifySurfaceExtent(64,64);
                    auto awaitFrame=[&](uint64_t count){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(runtime.framesRendered()<count && std::chrono::steady_clock::now()<until){runtime.rethrowFailure();std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(runtime.framesRendered()>=count,"Automatic quality stopped rendering");};
                    runtime.submit({first,{},{}});awaitFrame(1);
                    auto pressure=std::make_shared<RenderWorldSnapshot>(*first);pressure->automaticQuality=true;
                    OceanSurfaceSettings ocean;ocean.spectrum.size=2048;pressure->frame.oceans.push_back(ocean);
                    for(uint64_t i=2;i<=5;++i){runtime.submit({pressure,{},{}});awaitFrame(i);std::this_thread::sleep_for(std::chrono::milliseconds(260));}
                    runtime.finish();check(runtime.qualityLevel()>0 && runtime.rejectedPublications()>0 && runtime.rejectedPublications()<4 && pressure->frame.oceans[0].spectrum.size==2048,
                                          "Automatic quality did not recover bounded FFT allocation or mutated the CPU settings");
                }
                // Cold start must retry the same packet rather than silently dropping it.
                {
                    engine::RenderRuntime runtime(device,{},1);runtime.notifySurfaceExtent(64,64);
                    auto pressure=std::make_shared<RenderWorldSnapshot>(*first);pressure->automaticQuality=true;
                    OceanSurfaceSettings ocean;ocean.spectrum.size=2048;pressure->frame.oceans.push_back(ocean);
                    runtime.submit({pressure,{},{}});runtime.finish();
                    check(runtime.framesRendered()==1 && runtime.qualityLevel()>0,"Cold-start quality recovery dropped the initial packet");
                }
                device->setResourceBudget(before.budgetBytes);
            }
            bool propagated = false;
            try {
                engine::RenderRuntime runtime(device, {}, 1);
                auto invalid = std::make_shared<RenderWorldSnapshot>(*first);
                invalid->frame.ambient = -1;
                runtime.submit({invalid, {}, {}});
                runtime.finish();
            } catch (const std::invalid_argument &) {
                propagated = true;
            }
            check(propagated, "Render worker failure did not reach logic thread");
            device->checkThread();
            auto timing=device->gpuTimingStats();if(timing.supported)check(timing.samples>0 && std::isfinite(timing.peakMilliseconds) && timing.peakMilliseconds>=0,"Native GPU timings were not collected");
            device->savePipelineDiskCache();auto disk=device->pipelineDiskCacheStats();
            if(disk.supported && !disk.path.empty()) {
                check(disk.saved && disk.bytes>0 && std::filesystem::exists(disk.path),"Native pipeline cache was not persisted");
                std::shared_ptr<rhi::GraphicsDevice> fresh;
#ifdef SCENERENDERER_METAL
                if(device->backend()==rhi::Backend::Metal)fresh=rhi::makeMetalDevice();
#endif
#ifdef SCENERENDERER_HAS_VULKAN
                if(device->backend()==rhi::Backend::Vulkan)fresh=rhi::makeVulkanDevice();
#endif
                check(fresh && fresh->pipelineDiskCacheStats().loaded,"Compatible persisted pipeline cache was not loaded");fresh->close();
            }
        }
    }
    if (device->computeLimits().maxStorageImages) {
        auto scene = std::make_shared<RenderScene>();
        scene->setCamera(std::make_shared<Camera>(glm::vec3(0, 6, 12)));
        auto object = std::make_shared<Terrain>();
        auto terrain = std::make_shared<TerrainComponent>();
        terrain->setHeightData(32,32,std::vector<float>(32*32,.25f));
        object->addComponent(terrain);
        scene->addTerrain(object);
        SceneAdapter adapter(device);
        auto first = adapter.collect(scene, 0);
        auto oldMesh = first.packets.at(0).mesh;
        MeshVertex vertex;
        device->readBuffer(oldMesh->vertexBuffer(), 0, sizeof(vertex), &vertex);
        check(std::abs(vertex.position.y - .25f) < 1e-6f, "Terrain scene did not use initial source");
        terrain->setHeightData(32,32,std::vector<float>(32*32,.75f));
        terrain->invalidateHeight();
        auto next = adapter.collect(scene, 0);
        check(next.packets.at(0).mesh != oldMesh && next.frame.historyKey != first.frame.historyKey,
              "Terrain source revision did not rebuild geometry and invalidate history");
        device->readBuffer(next.packets.at(0).mesh->vertexBuffer(), 0, sizeof(vertex), &vertex);
        check(std::abs(vertex.position.y - .75f) < 1e-6f, "Terrain retained old source after invalidation");
        first.packets.clear();
        next.packets.clear();
        oldMesh.reset();
        object->addComponent(std::make_shared<Grass>());
        auto withGrass = adapter.collect(scene, 0);
        check(withGrass.packets.size() == 2, "Adding grass did not rebuild terrain cache");
        auto terrainMesh=withGrass.packets[0].mesh,grassMesh=withGrass.packets[1].mesh;
        auto vegetation=object->getComponent<Grass>();
        vegetation->updateSettings([](auto& settings){settings.density=0;settings.distance=40;settings.fadeStart=25;});
        auto editedGrass=adapter.collect(scene,1);
        check(editedGrass.packets[0].mesh==terrainMesh && editedGrass.packets[1].mesh==grassMesh,
              "Vegetation scalar change rebuilt terrain or grass resources");
        rhi::DrawIndexedIndirectArguments grassArgs;
        device->readBuffer(grassMesh->indirectBuffer(),0,sizeof(grassArgs),&grassArgs);
        check(grassArgs.instanceCount==0,"Snapshot did not publish zero vegetation density");
        vegetation->updateSettings([](auto& settings){settings.capacity=4096;});
        auto resizedGrass=adapter.collect(scene,1);
        check(resizedGrass.packets[1].mesh->instanceCapacity()==4096,
              "Vegetation capacity change did not recreate bounded instance storage");
        editedGrass.packets.clear();resizedGrass.packets.clear();terrainMesh.reset();grassMesh.reset();
        withGrass.packets.clear();
        object->removeComponent<Grass>();
        check(adapter.collect(scene, 0).packets.size() == 1, "Removing grass did not rebuild terrain cache");
        scene->destroy();
    }
    std::cout << "Engine ownership, component insertion, repeated scene loading, worker errors and terrain "
                 "source/cache invalidation passed\n";
}
} // namespace render
