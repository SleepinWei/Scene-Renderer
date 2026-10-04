#pragma once
#include "renderer/rhi/SceneSnapshot.h"
namespace pt {
struct CaptureOptions {uint32_t terrainGrid=0,textureExtent=1024,oceanGrid=0,grassLimit=16384;bool grass=true;};
void validateCaptureOptions(const CaptureOptions &);
struct OceanSamples {uint32_t size=0;std::vector<float> displacement,normal,foam;};
// CPU-only complete terrain surface from source VT pages, independent of raster LOD/frustum.
render::SnapshotDraw freezeTerrain(const render::SnapshotTerrain &,const CaptureOptions & = {});
render::SnapshotDraw freezeOcean(const render::OceanSurfaceSettings &,const OceanSamples &large,
                                  const OceanSamples &detail,const CaptureOptions & = {});
// Must run on the RHI device owner thread. Returns CPU payloads; no GPU handles escape.
render::RenderWorldSnapshot captureProcedural(const render::RenderWorldSnapshot &,
                                             std::shared_ptr<rhi::GraphicsDevice>,const CaptureOptions & = {});
}
