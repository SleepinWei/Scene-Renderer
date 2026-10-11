#pragma once
#include "renderer/rhi/SceneSnapshot.h"
#include <string>
namespace pt {
struct WorldLodSettings {
    bool enabled=false;
    float maxError=.05f,targetRatio=.5f,maxUvError=.02f;
    uint32_t levels=8;
    void validate() const;
};
struct MeshLodLevel {uint64_t triangles=0,vertices=0;float cellSize=0,maxDisplacement=0,maxUvDisplacement=0;};
struct MeshLodReport {
    uint64_t sourceId=0,sourceTriangles=0,selectedTriangles=0,instances=0;
    uint32_t selectedLevel=0;float worldScaleBound=1;
    std::string preservation;
    std::vector<MeshLodLevel> levels;
};
struct WorldLodReport {
    uint64_t sourceTriangles=0,selectedTriangles=0,sourceUniqueTriangles=0,selectedUniqueTriangles=0;
    uint64_t cacheHits=0,retainedCacheBytes=0;
    double seconds=0;std::vector<MeshLodReport> meshes;
};
// Worker-safe bounded retention of immutable generated geometry. Lighting and
// material edits can reuse LOD/BLAS identities when protection rules stay equal.
class WorldLodCache {
public:
    explicit WorldLodCache(size_t byteBudget=128*1024*1024);
    ~WorldLodCache();
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
    friend render::RenderWorldSnapshot buildLowLodWorld(const render::RenderWorldSnapshot &,const WorldLodSettings &,WorldLodReport *,std::shared_ptr<WorldLodCache>);
};
// Detached PT-only world. Spatial clusters are restricted to edge-connected
// vertices; boundaries, attribute seams and unsafe optical materials are kept.
// Error is a vertex displacement bound, not a Hausdorff/transport guarantee.
render::RenderWorldSnapshot buildLowLodWorld(const render::RenderWorldSnapshot &,const WorldLodSettings &,WorldLodReport * = nullptr,std::shared_ptr<WorldLodCache> = {});
void writeWorldLodReport(const WorldLodReport &,const WorldLodSettings &,const std::string &path);
void validateWorldLod();
}
