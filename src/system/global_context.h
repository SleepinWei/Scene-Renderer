#pragma once
#include<memory>
#include<string>

class RenderManager;
class ResourceManager;

class RunTimeGlobalContext {
public:

	std::shared_ptr<RenderManager> renderManager;
	std::shared_ptr<ResourceManager> resourceManager;

};
