#pragma once
#include "rhi/GraphicsDevice.h"
#include <string>
namespace rhi {
std::string readShaderText(const std::string& path);
std::vector<uint32_t> readSpirv(const std::string& path);
std::string defaultShaderDirectory();
void validateComputeShaderLayout(const ComputePipelineDesc&);
void validateShaderLayout(const GraphicsPipelineDesc&);
}
