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
#include "component/Lights.h"
#include "engine/RenderRuntime.h"
#include "rhi/ShaderAssets.h"
#include "object/Terrain.h"
#include "utils/Camera.h"
#include "system/Loader.h"
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
            check(first->width == 1 && first->height == 1 && first->data &&
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
        } catch (const json::parse_error &) {
            rejected = true;
        }
        check(rejected && scene->revision() == revision, "Loader cleared scene before validating JSON");
        rejected = false;
        try {
            loader->loadSceneAsync(scene, (base / "worker.json").string());
        } catch (const json::parse_error &) {
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
        auto scene = makeForwardDemoScene();
        auto seedTexture = std::make_shared<Texture>();
        seedTexture->width = 4;
        seedTexture->height = 1;
        seedTexture->channels = 4;
        seedTexture->format = GL_RGBA;
        seedTexture->data = static_cast<unsigned char *>(std::malloc(16));
        std::fill(seedTexture->data, seedTexture->data + 16, 128);
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
        replacement->width = replacement->height = 1;
        replacement->channels = 4;
        replacement->format = GL_RGBA;
        replacement->data = static_cast<unsigned char *>(std::malloc(4));
        replacement->data[0] = 7;
        replacement->data[1] = 11;
        replacement->data[2] = 19;
        replacement->data[3] = 255;
        material->addTexture(replacement, "material.albedo");
        auto imageEdit = builder.capture(scene, 4, 64, 64);
        check(imageEdit->draws[0].material != scalarEdit->draws[0].material &&
                  imageEdit->draws[0].material->id == scalarEdit->draws[0].material->id &&
                  imageEdit->draws[0].material->images[0] &&
                  scalarEdit->draws[0].material->images[0] &&
                  imageEdit->draws[0].material->images[0]->pixels[0] == 7 &&
                  scalarEdit->draws[0].material->images[0]->width == 4,
              "Texture replacement failed to rebuild immutable payload or modified earlier frame");
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
        }
    }
    if (device->computeLimits().maxStorageImages) {
        auto scene = std::make_shared<RenderScene>();
        scene->setCamera(std::make_shared<Camera>(glm::vec3(0, 6, 12)));
        auto object = std::make_shared<Terrain>();
        auto terrain = std::make_shared<TerrainComponent>();
        terrain->heightWidth = terrain->heightHeight = 32;
        terrain->heightData = new float[32 * 32];
        std::fill_n(terrain->heightData, 32 * 32, .25f);
        object->addComponent(terrain);
        scene->addTerrain(object);
        SceneAdapter adapter(device);
        auto first = adapter.collect(scene, 0);
        auto oldMesh = first.packets.at(0).mesh;
        MeshVertex vertex;
        device->readBuffer(oldMesh->vertexBuffer(), 0, sizeof(vertex), &vertex);
        check(std::abs(vertex.position.y - .25f) < 1e-6f, "Terrain scene did not use initial source");
        std::fill_n(terrain->heightData, 32 * 32, .75f);
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
        withGrass.packets.clear();
        object->removeComponent<Grass>();
        check(adapter.collect(scene, 0).packets.size() == 1, "Removing grass did not rebuild terrain cache");
        scene->destroy();
    }
    std::cout << "Engine ownership, component insertion, repeated scene loading, worker errors and terrain "
                 "source/cache invalidation passed\n";
}
} // namespace render
