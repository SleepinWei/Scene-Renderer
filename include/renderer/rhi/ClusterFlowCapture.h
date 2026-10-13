#pragma once
#include "renderer/rhi/SceneAdapter.h"
#include <json/json.hpp>
namespace render {
struct ClusterFlowView {
    std::string label;
    glm::vec3 eye, target;
    float pixelError=4;
};
nlohmann::json captureClusterFlow(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory,
    const std::string& outputDirectory,const RenderWorldSnapshot&,const SceneFrame&,
    const std::vector<ClusterFlowView>& views={});
void captureClusterSceneFlow(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,const std::string& scene,const std::string& outputDirectory);
void captureDenseClusterScene(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,const std::string& scene,const std::string& outputDirectory);
void captureClusterBuildComparison(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,const std::string& scene,const std::string& outputDirectory);
}
