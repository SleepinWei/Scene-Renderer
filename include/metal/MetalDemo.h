#pragma once
#include <memory>
#include <string>
class RenderScene;
std::shared_ptr<RenderScene> makeMetalDemoScene();
void validateMetalFeatures();
void validateMetalRSM();
void validateMetalOcean();
void validateMetalWaterOptics();
std::shared_ptr<RenderScene> makeMetalOceanScene(bool clearWater=false);
std::shared_ptr<RenderScene> makeMetalClassicScene(const std::string& name);
void renderMetalGallery(const std::string& directory, const std::string& selected = "");
