#include"transform.h"
Transform::Transform() :position(0.f), rotation(0.f), scale(1.0f) {
	Component::name = "Transform";

}
Transform::~Transform() {

}

void Transform::loadFromJson(json& data) {
	glm::vec3 position_(
		data["position"][0].get<float>(),
		data["position"][1].get<float>(),
		data["position"][2].get<float>()
	);
	glm::vec3 rotation_(
		data["rotation"][0].get<float>(),
		data["rotation"][1].get<float>(),
		data["rotation"][2].get<float>()
	);
	glm::vec3 scale_(
		data["scale"][0].get<float>(),
		data["scale"][1].get<float>(),
		data["scale"][2].get<float>()
	);
	this->position = position_;
	this->rotation = rotation_;
	this->scale = scale_;
}