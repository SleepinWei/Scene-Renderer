#pragma once
#include<glm/glm.hpp>
#include<memory>

class GameObject; 

class Component{
public:
	Component() =default;
	virtual ~Component() {};
	void setGameObject(std::shared_ptr<GameObject> object) {
		gameObject = object;
	}
public:
	std::shared_ptr<GameObject> gameObject;
};

