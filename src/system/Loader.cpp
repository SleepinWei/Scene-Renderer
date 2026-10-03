#include "system/Loader.h"
#include "renderer/RenderScene.h"
#include "component/GameObject.h"
#include "object/Terrain.h"
#include "object/SkyBox.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "utils/Camera.h"
#include "rhi/Device.h"
#include <fstream>
#include <stdexcept>
namespace {
json readJson(const std::string &path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open scene resource: " + path);
    return json::parse(file);
}
void checkCancelled(const std::shared_ptr<std::atomic<bool>> &cancelled) {
    if (cancelled->load())
        throw std::runtime_error("Scene load cancelled");
}
} // namespace
void Loader::loadObject(std::shared_ptr<RenderScene> &scene, const std::string &filename) {
    auto data = readJson(filename);
    auto object = std::make_shared<GameObject>();
    object->loadFromJson(data);
    scene->addObject(object);
}
void Loader::loadSky(std::shared_ptr<RenderScene> scene, const std::string filename) {
    auto data = readJson(filename);
    auto sky = std::make_shared<Sky>();
    sky->loadFromJson(data);
    scene->addSky(sky);
}
void Loader::loadTerrain(std::shared_ptr<RenderScene> scene, const std::string filename) {
    auto data = readJson(filename);
    auto terrain = std::make_shared<Terrain>();
    terrain->loadFromJson(data);
    scene->addTerrain(terrain);
}
SceneLoadRequest Loader::buildScene(const std::string &filename) {
    SceneLoadRequest request;
    request.cancelled = std::make_shared<std::atomic<bool>>(false);
    request.completed = std::make_shared<std::atomic<size_t>>(0);
    request.total = std::make_shared<std::atomic<size_t>>(0);
    auto cancelled = request.cancelled;
    auto completed = request.completed;
    auto total = request.total;
    if (!rhi::usesNativeRenderer()) {
        std::promise<std::shared_ptr<RenderScene>> rejected;
        request.result = rejected.get_future();
        rejected.set_exception(std::make_exception_ptr(std::logic_error(
            "Background scene build requires the native renderer; legacy GL loads on its context thread")));
        return request;
    }
    auto task = std::make_shared<std::packaged_task<std::shared_ptr<RenderScene>()>>(
        [this, filename, cancelled, completed, total] {
            checkCancelled(cancelled);
            auto data = readJson(filename);
            auto staging = std::make_shared<RenderScene>();
            std::vector<std::future<std::shared_ptr<GameObject>>> objects;
            std::future<std::shared_ptr<Sky>> sky;
            std::future<std::shared_ptr<Terrain>> terrain;
            std::exception_ptr failure;
            try {
                if (data.contains("objects")) {
                    if (!data.at("objects").is_object())
                        throw std::invalid_argument("Scene objects must be a path dictionary");
                    total->fetch_add(data.at("objects").size());
                    for (const auto &entry : data.at("objects").items()) {
                        auto path = entry.value().get<std::string>();
                        objects.push_back(decode_.submit([path, cancelled, completed] {
                            checkCancelled(cancelled);
                            auto json = readJson(path);
                            auto object = std::make_shared<GameObject>();
                            object->loadFromJson(json);
                            checkCancelled(cancelled);
                            object->sealForTransfer();
                            completed->fetch_add(1);
                            return object;
                        }));
                    }
                }
                if (data.contains("sky")) {
                    total->fetch_add(1);
                    auto path = data.at("sky").get<std::string>();
                    sky = decode_.submit([path, cancelled, completed] {
                        checkCancelled(cancelled);
                        auto json = readJson(path);
                        auto object = std::make_shared<Sky>();
                        object->loadFromJson(json);
                        checkCancelled(cancelled);
                        object->sealForTransfer();
                        completed->fetch_add(1);
                        return object;
                    });
                }
                if (data.contains("terrain")) {
                    total->fetch_add(1);
                    auto path = data.at("terrain").get<std::string>();
                    terrain = decode_.submit([path, cancelled, completed] {
                        checkCancelled(cancelled);
                        auto json = readJson(path);
                        auto object = std::make_shared<Terrain>();
                        object->loadFromJson(json);
                        checkCancelled(cancelled);
                        object->sealForTransfer();
                        completed->fetch_add(1);
                        return object;
                    });
                }
            } catch (...) {
                failure = std::current_exception();
            }
            // Drain every issued future even when one fails. The old world is untouched.
            for (auto &object : objects)
                try {
                    staging->addObject(object.get());
                } catch (...) {
                    if (!failure)
                        failure = std::current_exception();
                }
            if (sky.valid())
                try {
                    staging->addSky(sky.get());
                } catch (...) {
                    if (!failure)
                        failure = std::current_exception();
                }
            if (terrain.valid())
                try {
                    staging->addTerrain(terrain.get());
                } catch (...) {
                    if (!failure)
                        failure = std::current_exception();
                }
            if (failure)
                std::rethrow_exception(failure);
            checkCancelled(cancelled);
            if (rhi::usesNativeRenderer()) {
                // Decode/validate every referenced native asset and VT bootstrap page
                // before publication. Reuse those detached payloads on the logic thread.
                staging->setCamera(std::make_shared<Camera>());
                render::SceneSnapshotBuilder prepare;
                staging->setPreparedAssets(prepare.capture(staging, 0, 64, 64, true));
                staging->setCamera({});
            }
            checkCancelled(cancelled);
            staging->sealForTransfer();
            return staging;
        });
    request.result = task->get_future();
    if (!coordinator_.tryEnqueue([task] { (*task)(); })) {
        std::promise<std::shared_ptr<RenderScene>> rejected;
        request.result = rejected.get_future();
        rejected.set_exception(std::make_exception_ptr(std::runtime_error("Scene load queue is full")));
    }
    return request;
}
void Loader::loadSceneAsync(std::shared_ptr<RenderScene> &scene, const std::string &filename) {
    if (!scene)
        throw std::invalid_argument("Loader needs a destination scene");
    if (!rhi::usesNativeRenderer()) {
        // Legacy component constructors can create GL objects. Keep them on the
        // context thread, while still preserving the transaction on failure.
        auto data = readJson(filename);
        auto staging = std::make_shared<RenderScene>();
        if (data.contains("objects"))
            for (const auto &entry : data.at("objects").items())
                loadObject(staging, entry.value().get<std::string>());
        if (data.contains("sky"))
            loadSky(staging, data.at("sky").get<std::string>());
        if (data.contains("terrain"))
            loadTerrain(staging, data.at("terrain").get<std::string>());
        scene->replaceWith(*staging);
        return;
    }
    auto request = buildScene(filename);
    auto built = request.result.get();
    scene->replaceWith(*built);
}

void Loader::waitIdle() {
    coordinator_.submit([] {}).get();
}
