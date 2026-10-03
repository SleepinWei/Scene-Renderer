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
#include "engine/RenderRuntime.h"
#include "rhi/ShaderAssets.h"
#include "object/Terrain.h"
#include "utils/Camera.h"
#include "system/Loader.h"
#include "system/ResourceManager.h"
#include "renderer/Texture.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>
namespace render {
void validateEngineBasics(std::shared_ptr<rhi::GraphicsDevice> device) {
    auto check = [](bool value, const char *reason) {
        if (!value)
            throw std::runtime_error(reason);
    };
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
        }
        auto loader = Loader::GetInstance();
        loader->loadSceneAsync(scene, (base / "empty.json").string());
        loader->loadSceneAsync(scene, (base / "empty.json").string());
        auto sentinel = std::make_shared<GameObject>("keep on failure");
        scene->addObject(sentinel);
        scene->main_camera = std::make_shared<Camera>();
        auto camera = scene->main_camera;
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
        check(scene->objects.size() == 1 && scene->objects[0] == sentinel && scene->main_camera == camera,
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
        check(rejected && scene->objects.at(0) == sentinel && scene->revision() == revision,
              "Missing child file was silently accepted or cleared the world");
        if (device->backend() != rhi::Backend::OpenGL) {
            auto request = loader->buildScene((base / "empty.json").string());
            auto built = request.result.get();
            check(scene->objects.at(0) == sentinel && scene->revision() == revision,
                  "Async build published before logic-thread commit");
            scene->replaceWith(*built);
            check(scene->objects.empty() && scene->main_camera == camera && scene->revision() > revision,
                  "Successful commit lost camera or failed to replace world");
        }
        loader->loadSceneAsync(scene, (base / "empty.json").string());
        loader->waitIdle();
        std::filesystem::remove_all(base);
    }
    {
        auto scene = makeForwardDemoScene();
        SceneSnapshotBuilder builder;
        auto first = builder.capture(scene, 0, 64, 64);
        check(!first->draws.empty(), "Snapshot lost scene draws");
        auto unchanged = builder.capture(scene, 1, 64, 64);
        check(first->draws[0].mesh == unchanged->draws[0].mesh, "Unchanged geometry was recopied each frame");
        std::shared_ptr<GameObject> object;
        for (const auto &candidate : scene->objects)
            if (candidate->assetId == first->draws[0].objectId)
                object = candidate;
        check(bool(object), "Snapshot object identity is not stable");
        auto filter = object->getComponent<MeshFilter>();
        auto transform = object->getComponent<Transform>();
        auto mesh = filter->meshes[0];
        auto oldPosition = first->draws[0].mesh->vertices[0].position;
        transform->position.x += 10;
        for (auto &vertex : mesh->vertices)
            vertex.Position.x += 1;
        mesh->invalidate();
        auto updated = builder.capture(scene, 2, 64, 64);
        check(first->draws[0].mesh->vertices[0].position == oldPosition &&
                  first->draws[0].model != updated->draws[0].model,
              "Published snapshot aliases mutable scene state");
        check(first->draws[0].mesh != updated->draws[0].mesh &&
                  first->draws[0].mesh->id == updated->draws[0].mesh->id,
              "Asset revision did not preserve ID and rebuild immutable payload");
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
