#pragma once
#include"Component.h"
#include<glm/glm.hpp>
#include<json/json.hpp>

using json = nlohmann::json;

class Transform : public Component {
public:
	Transform();
	~Transform();

	virtual void loadFromJson(json& data);
public:
	glm::vec3 position;
	glm::vec3 rotation;
	glm::vec3 scale; 
};
