#pragma once
#include "engine/AssetIdentity.h"
#include<glm/glm.hpp>
#include<memory>
#include<string>
#include<stdexcept>
#include<json/json.hpp>

using json = nlohmann::json;

class GameObject; 

class Component : public engine::AssetIdentity {
public:
	Component() =default;
	virtual ~Component() {};
	void setGameObject(std::shared_ptr<GameObject> object) {
		gameObject = object;
	}
    std::shared_ptr<GameObject> owner()const{auto object=gameObject.lock();if(!object)throw std::logic_error("Component owner expired or not assigned");return object;}
	virtual void loadFromJson(json& data) {};
public:
	std::weak_ptr<GameObject> gameObject;
	std::string name; 
};

