#include "renderer/rhi/FeatureScenes.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/RenderScene.h"
#include "component/Atmosphere.h"
#include "component/Lights.h"
#include "object/SkyBox.h"
#include <cmath>
#include <iostream>
namespace render {
void validateSceneSolarControls(std::shared_ptr<rhi::GraphicsDevice> device) {
    auto check=[](bool value,const char* reason){if(!value)throw std::runtime_error(reason);};
    // Verify source authority, GUI edits, and external light edits across real scene collection.
    {
        auto scene=makeClassicScene("sky");SceneAdapter adapter(device);
        auto atmo=std::static_pointer_cast<Atmosphere>(scene->sky()->GetComponent("Atmosphere"));
        auto light=scene->directionLights().at(0);light->setDirection(glm::normalize(glm::vec3(-1,-1,0)));
        auto frame=adapter.collect(scene,0);
        check(std::abs(frame.frame.sunAngle-45)<.001f && std::abs(frame.frame.sunAzimuth-90)<.001f,"Initial sky did not adopt authored DirectionLight");
        auto color=light->getData().color;atmo->updateSettings([&](auto& value){value.sunAngle=20;});atmo->updateSettings([&](auto& value){value.sunAzimuth=-35;});frame=adapter.collect(scene,0);
        auto wanted=-glm::vec3(std::cos(glm::radians(20.f))*std::sin(glm::radians(-35.f)),std::sin(glm::radians(20.f)),-std::cos(glm::radians(20.f))*std::cos(glm::radians(-35.f)));
        check(glm::length(light->getData().direction-wanted)<1e-5f && light->getData().color ==color,"Sky GUI did not update the same solar light without changing its energy");
        light->setDirection({0,-1,0});frame=adapter.collect(scene,0);check(std::abs(frame.frame.sunAngle-90)<.001f,"External DirectionLight edits did not return to sky controls");
        scene->destroy();
    }
    std::cout<<"RHI scene solar controls: initial authority, GUI edits and external light synchronization passed\n";
}
}
