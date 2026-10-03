#pragma once
#include <memory>
#include <string>
class RenderScene;
namespace render {
void runFeatureGallery(const std::string& directory,const std::string& selection);
std::shared_ptr<RenderScene> makeFeatureScene();
std::shared_ptr<RenderScene> makeOceanScene(bool clearWater = false);
std::shared_ptr<RenderScene> makeClassicScene(const std::string& name);
}
