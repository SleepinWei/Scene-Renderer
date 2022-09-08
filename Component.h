#pragma once
#include<glm/glm.hpp>

class ComponentBase {
public:
	ComponentBase() {};
	virtual ~ComponentBase() {};
	virtual void tick(float delta_time) {};
};

class TransformComponent:virtual public ComponentBase {
public:
	//functions 
	TransformComponent() = default;
	~TransformComponent();

	//void updatePosition(glm::vec4 offset);
	
	//variables
	glm::vec4 position; 
	glm::vec4 rotation; 
	glm::vec4 scale;
};


