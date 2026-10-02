#pragma once
#include <memory>
#include <string>
class RenderScene;
std::shared_ptr<RenderScene> makeMetalDemoScene();
void validateMetalFeatures();
void validateMetalRSM();
std::shared_ptr<RenderScene> makeMetalClassicScene(const std::string& name);
void renderMetalGallery(const std::string& directory, const std::string& selected = "");
