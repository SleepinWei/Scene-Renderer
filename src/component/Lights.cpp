#include"Lights.h"
#include<glm/gtc/matrix_transform.hpp>
#include"../component/GameObject.h"
#include"../component/transform.h"
#include"../renderer/Texture.h"
Light::Light()
{

}

Light::~Light() {

}

void Light::setDirtyFlag(bool dirty) {
	this->dirty = dirty;
}

PointLight::PointLight() {
	Component::name = "PointLight";
	Light::type = LightType::POINT;
	Light::castShadow = true; 
	Light::dirty = true;

	data = {
		glm::vec3(1.0f),//color
		//glm::vec3(0.0f),//position

		glm::vec3(1.0f),//ambient
		glm::vec3(1.0f),//diffuse
		glm::vec3(1.0f), //specular
		1, //constant
		0, //linear
		0  //quadratic
	};

	//mode = POINTLIGHT::SINGLE;
	aspect = Light::SHADOW_HEIGHT * 1.0 / Light::SHADOW_WIDTH;
	fov = 90.0f; 
	near = 0.1f; 
	far = 100.f;

	// TODO: add texture generation
	//shadowTex = std::make_shared<Texture>();
	//shadowTex->genCubeMap(GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT);
}

tuple<glm::mat4,glm::mat4> PointLight::getLightTransform(int face) {

	//glm::vec3 lightPos = data.position;
	// TODO: confirm the sequence is correct
	static vector<glm::vec3> centers{
		glm::vec3(1.0, 0.0, 0.0),
		glm::vec3(-1.0, 0.0, 0.0),
		glm::vec3(0.0, 1.0, 0.0),
		glm::vec3(0.0, -1.0, 0.0),
		glm::vec3(0.0, 0.0, 1.0),
		glm::vec3(0.0, 0.0, -1.0)
	};
	static vector<glm::vec3> ups{
		glm::vec3(0.0, -1.0, 0.0),
		glm::vec3(0.0, -1.0, 0.0),
		glm::vec3(0.0, 0.0, 1.0),
		glm::vec3(0.0, 0.0, -1.0),
		glm::vec3(0.0, -1.0, 0.0),
		glm::vec3(0.0, -1.0, 0.0)
	};

	glm::vec3 lightPos = std::static_pointer_cast<Transform>(
		gameObject->GetComponent("Transform"))->position;

	//if (Light:: dirty) {
	glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, near, far);
	glm::mat4 view = glm::lookAt(lightPos, lightPos + centers[face], ups[face]);
	//}
	return std::make_tuple(proj, view);
}

PointLight::~PointLight() {

}

std::shared_ptr<PointLight> PointLight::setCastShadow(bool castShadow_) {
	this->castShadow = castShadow_;
	return std::dynamic_pointer_cast<PointLight>(shared_from_this());
}

void PointLight::loadFromJson(json& json_data) {
	if (json_data.find("color") != json_data.end()) {
		for (int i = 0; i < 3; i++) {
			data.color[i] = json_data["color"][i].get<float>();
		}
	}
}

DirectionLight::DirectionLight() {
	Component::name = "DirectionLight";
	Light::type = LightType::DIRECTIONAL;
	Light::castShadow = true;
	Light::dirty = true;

	near = 0.1f;
	far = 10.0f;
	ortho_width = 10.0f;

	data = {
		glm::vec3(1.0f), // color
		//glm::vec3(0.0f),//position
		glm::vec3(0.0f,-1.0f,0.0f), // direction
		glm::vec3(1.0f),//ambient
		glm::vec3(1.0f),//diffuse
		glm::vec3(1.0f)//specular
	};

	// TODO: add Texture generation
	shadowTex = std::make_shared<Texture>();
	//shadowTex->genTexture(GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT);
}

DirectionLight::~DirectionLight() {

}

tuple<glm::mat4,glm::mat4> DirectionLight::getLightTransform() {
	//if (Light::dirty) {
		//glm::vec3 lightPos = data.position;
		glm::vec3 lightPos = std::static_pointer_cast<Transform>(
			gameObject->GetComponent("Transform")
			)->position;
		glm::mat4 proj = glm::ortho(-ortho_width, ortho_width, -ortho_width, ortho_width, near, far);
		glm::mat4 view = glm::lookAt(lightPos, lightPos + data.direction, glm::vec3(0.0f, 1.0f, 0.0f));
		//lightProj = proj;
	//}
	return std::make_tuple(view,proj);
}

std::shared_ptr<DirectionLight> DirectionLight::setDirection(const glm::vec3& dir) {
	data.direction = dir; 
	return std::dynamic_pointer_cast<DirectionLight>(shared_from_this());
}

std::shared_ptr<DirectionLight> DirectionLight::setCastShadow(bool castShadow_) {
	this->castShadow = castShadow_;
	return std::dynamic_pointer_cast<DirectionLight>(shared_from_this());
}

void DirectionLight::loadFromJson(json& json_data) {
	if (json_data.find("color") != json_data.end()) {
		for (int i = 0; i < 3; i++) {
			data.color[i] = json_data["color"][i].get<float>();
		}
	}
	if (json_data.find("direction") != json_data.end()) {
		auto& dir = json_data["direction"];
		for (int i = 0; i < 3; i++) {
			data.direction[i] = dir[i].get<float>();
		}
	}
}

SpotLight::SpotLight()
{
	Component::name = "SpotLight";
	Light::type = LightType::SPOT;
	Light::castShadow = true;
	Light::dirty = true;

	data = {
		glm::vec3(1.0f),//color
		//glm::vec3(0.0f),//position
		glm::vec3(1.0f),//direction
		glm::cos(glm::radians(12.5f)),//cutoff
		glm::cos(glm::radians(15.0f)),//outercutoff
		1.0f, //constant
		0.09f, //linear
		0.032f,  //quadratic
		glm::vec3(0.0f),//ambient
		glm::vec3(1.0f),//diffuse
		glm::vec3(1.0f), //specular
	};

	aspect = Light::SHADOW_HEIGHT * 1.0 / Light::SHADOW_WIDTH;
	fov = 90.0f;
	near = 0.1f;
	far = 100.f;
}

SpotLight::~SpotLight()
{

}

tuple<glm::mat4, glm::mat4> SpotLight::getLightTransform() {
		glm::vec3 lightPos = std::static_pointer_cast<Transform>(
			gameObject->GetComponent("Transform")
			)->position;
		glm::mat4 proj = glm::perspective(glm::radians(fov), (float)Light::SHADOW_WIDTH / (float)Light::SHADOW_HEIGHT, near, far);
		glm::mat4 view = glm::lookAt(lightPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		//lightProj = proj;
	//}
	return std::make_tuple(view, proj);
}

void SpotLight::loadFromJson(json& json_data) {
	if (json_data.find("color") != json_data.end()) {
		for (int i = 0; i < 3; i++) {
			data.color[i] = json_data["color"][i].get<float>();
		}
	}
	if (json_data.find("direction") != json_data.end()) {
		auto& dir = json_data["direction"];
		for (int i = 0; i < 3; i++) {
			data.direction[i] = dir[i].get<float>();
		}
	}
	if (json_data.find("cutOff") != json_data.end()) {
		data.cutOff = json_data["cutOff"].get<float>();
		data.cutOff = glm::cos(glm::radians(data.cutOff));
	}
	if (json_data.find("outerCutOff") != json_data.end()) {
		data.outerCutOff = json_data["outerCutOff"].get<float>();
		data.outerCutOff = glm::cos(glm::radians(data.outerCutOff));
	}
}