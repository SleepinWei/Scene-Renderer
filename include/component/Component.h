#pragma once
#include<glm/glm.hpp>
#include<memory>
#include<string>
#include<json/json.hpp>

using json = nlohmann::json;

class GameObject; 

class Component{
public:
	Component() =default;
	virtual ~Component() {};
	void setGameObject(std::shared_ptr<GameObject> object) {
		gameObject = object;
	}
	virtual void loadFromJson(json& data) {};
public:
	std::shared_ptr<GameObject> gameObject;
	std::string name; 
};

