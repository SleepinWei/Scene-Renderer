#pragma once
#include<memory>
#include<string>

class RenderManager;
class ResourceManager;
class InputManager;

std::unique_ptr<RenderManager> renderManager;
std::unique_ptr<ResourceManager> resourceManager;
std::unique_ptr<InputManager> inputManager;

