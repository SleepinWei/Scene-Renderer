#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
namespace render {
// Explicit offline capture. Saves actual GPU mips/arguments and two matched
// renderer outputs; never called from the interactive frame loop.
void exportHiZVisualization(ForwardPbrRenderer&,const FrameData&,
    const std::vector<DrawPacket>&,const std::string& directory,const std::string& name,float exposure=1);
}
