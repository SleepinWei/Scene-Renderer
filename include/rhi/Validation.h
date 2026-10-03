#pragma once
#include <memory>
namespace rhi {
class Device;
class GraphicsDevice;
void validateBufferTransfers(Device&, bool legacyBinding = true);
void validateComputeAndIndirect(GraphicsDevice&);
void validateFrameLifecycle(std::shared_ptr<GraphicsDevice>);
void validateTexturedRendering(GraphicsDevice&);
}
