#pragma once
#include "engine/Environment.h"
#include "renderer/rhi/SceneSnapshot.h"
namespace render {
engine::EnvironmentConfig environmentConfig(const RenderWorldSnapshot&,const std::string& sceneKey);
void applyEnvironment(RenderWorldSnapshot&,const engine::EnvironmentSnapshot&);
}
