#pragma once
#include<string>
#include<memory>

class Component;

class Meta{
public:
	Meta();
	~Meta();

	static std::shared_ptr<Component> generateComponent(const std::string& name);
};
