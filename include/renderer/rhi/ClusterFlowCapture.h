#pragma once
#include "renderer/rhi/SceneAdapter.h"
#include <json/json.hpp>
namespace render {
nlohmann::json captureClusterFlow(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory,
    const std::string& outputDirectory,const RenderWorldSnapshot&,const SceneFrame&);
void captureDenseClusterScene(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,const std::string& scene,const std::string& outputDirectory);
void captureClusterBuildComparison(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,const std::string& scene,const std::string& outputDirectory);
}
