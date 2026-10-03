#include "renderer/rhi/FeatureScenes.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/RenderScene.h"
#include "component/TerrainComponent.h"
#include "component/Grass.h"
#include "component/GameObject.h"
#include "object/Terrain.h"
#include "utils/Camera.h"
#include "system/Loader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
namespace render {
void validateEngineBasics(std::shared_ptr<rhi::GraphicsDevice> device) {
    auto check = [](bool value, const char *reason) {
        if (!value)
            throw std::runtime_error(reason);
    };
    {
        auto object = std::make_shared<GameObject>("ownership regression");
        auto terrain = std::make_shared<TerrainComponent>();
        object->addComponent(terrain);
        check(object->addComponent<TerrainComponent>() == terrain,
              "Template component insertion returned an unattached duplicate");
        check(object->name == "ownership regression" && terrain->owner() == object,
              "Object name or component owner lost");
        auto duplicate = std::make_shared<TerrainComponent>();
        object->addComponent(duplicate);
        check(duplicate->gameObject.expired(), "Rejected duplicate component retained an owner");
        std::weak_ptr<GameObject> weakObject = object;
        object.reset();
        check(weakObject.expired() && terrain->gameObject.expired(),
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
        auto base = std::filesystem::temp_directory_path() / "scene-renderer-loader-validation";
        std::filesystem::create_directories(base);
        {
            std::ofstream file(base / "empty.json");
            file << "{}";
            std::ofstream bad(base / "bad.json");
            bad << "{";
            std::ofstream terrain(base / "terrain.json");
            terrain << "{";
            std::ofstream worker(base / "worker.json");
            worker << "{\"terrain\":\"" << (base / "terrain.json").string() << "\"}";
        }
        auto loader = Loader::GetInstance();
        loader->loadSceneAsync(scene, (base / "empty.json").string());
        loader->loadSceneAsync(scene, (base / "empty.json").string());
        check(loader->threadpool.empty(), "Loader retained joined threads");
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
        check(rejected && loader->threadpool.empty(),
              "Loader lost worker exception or left unjoined threads");
        loader->loadSceneAsync(scene, (base / "empty.json").string());
        std::filesystem::remove_all(base);
    }
    if (device->computeLimits().maxStorageImages) {
        auto scene = std::make_shared<RenderScene>();
        scene->main_camera = std::make_shared<Camera>(glm::vec3(0, 6, 12));
        auto object = std::make_shared<Terrain>();
        auto terrain = std::make_shared<TerrainComponent>();
        terrain->heightWidth = terrain->heightHeight = 32;
        terrain->heightData = new float[32 * 32];
        std::fill_n(terrain->heightData, 32 * 32, .25f);
        object->addComponent(terrain);
        scene->terrain = object;
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
        object->component_type_instance_map.erase("Grass");
        check(adapter.collect(scene, 0).packets.size() == 1, "Removing grass did not rebuild terrain cache");
        scene->destroy();
    }
    std::cout << "Engine ownership, component insertion, repeated scene loading, worker errors and terrain "
                 "source/cache invalidation passed\n";
}
} // namespace render
