#include"Lights.h"
#include<glm/gtc/matrix_transform.hpp>
#include"../component/GameObject.h"
#include"../component/transform.h"
Light::Light() {
	name = "Light";
}

Light::~Light() {

}

PointLight::PointLight() {
	type = LightType::POINT;
	castShadow = true; 
	dirty = true;

	diffuse = glm::vec3(1.0f);
	specular = glm::vec3(1.0f);
	ambient = glm::vec3(1.0f);

	constant = 1; 
	linear = 0;
	quadratic = 0; 

	//mode = POINTLIGHT::SINGLE;
	aspect = Light::SHADOW_HEIGHT * 1.0 / Light::SHADOW_WIDTH;
	fov = 90.0f; 
	near = 0.1f; 
	far = 100.f;
}

//std::shared_ptr<PointLight> PointLight::setMode(POINTLIGHT mode) {
//	if (mode != this->mode) {
//		this->mode = mode;
//	}
//}

std::vector<glm::mat4> PointLight::getLightMatrix() {
	// get transform; 
	std::shared_ptr<Transform> transform = std::dynamic_pointer_cast<Transform>(gameObject->GetComponent("Transform"));
	if (transform == nullptr) {
		std::cerr << "In PointLight: No Transform Component" << '\n';
		return;
	}
	glm::vec3 lightPos = transform->position;

	if (dirty) {
		glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, near, far);

		lightTransforms.push_back(
			proj * glm::lookAt(lightPos, lightPos + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0));
		lightTransforms.push_back(
			proj * glm::lookAt(lightPos, lightPos + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0));
		lightTransforms.push_back(
			proj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0));
		lightTransforms.push_back(
			proj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0));
		lightTransforms.push_back(
			proj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0));
		lightTransforms.push_back(
			proj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0));
		dirty = false;
	}
	return lightTransforms;
}

PointLight::~PointLight() {

}

DirectionLight::DirectionLight() {
	type = LightType::DIRECTIONAL;
	castShadow = true;
	dirty = true;

	near = 0.1f;
	far = 7.5f;
}

DirectionLight::~DirectionLight() {

}

glm::mat4 DirectionLight::getLightMatrix() {
	if (dirty) {
		std::shared_ptr<Transform> trans = std::dynamic_pointer_cast<Transform> (gameObject->GetComponent("Transform"));
		glm::vec3 lightPos = trans->position;
		glm::mat4 proj = glm::ortho(-10.0f, 10.f, -10.0f, 10.0f, near, far);
		glm::mat4 view = glm::lookAt(lightPos, lightPos + direction, glm::vec3(0.0f, 1.0f, 0.0f));
		lightTransforms = proj * view; 

		dirty = true;
	}
	return lightTransforms; 
}

std::shared_ptr<DirectionLight> DirectionLight::setDirection(const glm::vec3& dir) {
	direction = dir; 
	return std::dynamic_pointer_cast<DirectionLight>(shared_from_this());
}


