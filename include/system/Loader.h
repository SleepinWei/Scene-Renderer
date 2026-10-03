#pragma once
#include "engine/JobSystem.h"
#include <atomic>
#include <memory>
#include <string>
class RenderScene;
struct SceneLoadRequest {
    std::future<std::shared_ptr<RenderScene>> result;
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::shared_ptr<std::atomic<size_t>> completed;
    std::shared_ptr<std::atomic<size_t>> total;
    void cancel() const {
        if (cancelled)
            cancelled->store(true);
    }
};
class Loader {
  public:
    static Loader *GetInstance() {
        static Loader loader;
        return &loader;
    }
    SceneLoadRequest buildScene(const std::string &filename);
    void waitIdle(); // Drain cancelled/obsolete requests before device shutdown.
    // Compatibility blocking wrapper. Commits only after the full build succeeds.
    void loadSceneAsync(std::shared_ptr<RenderScene> &scene, const std::string &filename);
    void loadObject(std::shared_ptr<RenderScene> &scene, const std::string &filename);
    void loadSky(std::shared_ptr<RenderScene> scene, const std::string filename);
    void loadTerrain(std::shared_ptr<RenderScene> scene, const std::string filename);

  private:
    Loader() = default;
    // Coordinator is separate: it may wait for decode workers without pool recursion.
    engine::JobSystem decode_{0, 128};
    engine::JobSystem coordinator_{1, 4};
};
