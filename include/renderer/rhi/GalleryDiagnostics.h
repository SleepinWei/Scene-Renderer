#pragma once
#include "renderer/rhi/SceneAdapter.h"
namespace render {
// Explicit offline readbacks; never called by the interactive frame loop.
void exportGalleryDiagnostics(const std::string& directory, const std::string& name,
    std::shared_ptr<rhi::GraphicsDevice>, const SceneAdapter&, ForwardPbrRenderer&, const FrameData&);
}
