#pragma once
#include <memory>
struct GLFWwindow;
namespace rhi {class GraphicsDevice;}
namespace pt {
// Uses the actual render owner thread / queue / Raster fallback and PT bridge.
void runViewportBenchmark(int argc,char **argv,std::shared_ptr<rhi::GraphicsDevice>,GLFWwindow*);
}
