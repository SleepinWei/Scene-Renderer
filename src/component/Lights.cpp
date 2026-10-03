#include"component/Lights.h"
#include <cmath>
#include<glm/gtc/matrix_transform.hpp>
#include"component/GameObject.h"
#include"component/transform.h"
#include"renderer/Texture.h"
Light::Light()
{
	enabled = true;
}

Light::~Light() {

}

void Light::setDirtyFlag(bool dirty) {
    checkLogicThread();
	this->dirty = dirty;
}

PointLight::PointLight() {
	Component::name = "PointLight";
	Light::type = LightType::POINT;
	Light::castShadow = true; 
	Light::dirty = true;

	data_ = {
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
	aspect_ = Light::SHADOW_HEIGHT * 1.0 / Light::SHADOW_WIDTH;
	fov_ = 90.0f;
	near_ = 0.1f;
	far_ = 100.f;

	// TODO: add texture generation
	shadowTex = std::make_shared<Texture>();
	//shadowTex->genCubeMap(GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT);
}

tuple<glm::mat4,glm::mat4> PointLight::getLightTransform(int face) {
    checkLogicThread();if(face<0 || face>=6)throw std::invalid_argument("Point light face out of range");

	//glm::vec3 lightPos = data_.position;
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
		owner()->GetComponent("Transform"))->getPosition();

	//if (Light:: dirty) {
	glm::mat4 proj = glm::perspective(glm::radians(fov_), aspect_, near_, far_);
	glm::mat4 view = glm::lookAt(lightPos, lightPos + centers[face], ups[face]);
	//}
	return std::make_tuple(proj, view);
}

PointLight::~PointLight() {

}

std::shared_ptr<PointLight> PointLight::setCastShadow(bool castShadow_) {
    checkLogicThread();
	this->castShadow = castShadow_;dirty=true;invalidate();
	return std::dynamic_pointer_cast<PointLight>(shared_from_this());
}

void PointLight::loadFromJson(json& json_data) {
    checkLogicThread();
    auto candidate=getData();
	if (json_data.find("color") != json_data.end()) {
		for (int i = 0; i < 3; i++) {
			candidate.color[i] = json_data["color"][i].get<float>();
		}
	}
    setData(candidate);
}

DirectionLight::DirectionLight() {
	Component::name = "DirectionLight";
	Light::type = LightType::DIRECTIONAL;
	Light::castShadow = true;
	Light::dirty = true;

	near_ = 0.1f;
	far_ = 10.0f;
	ortho_width_ = 10.0f;

	data_ = {
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
    checkLogicThread();
	//if (Light::dirty) {
		//glm::vec3 lightPos = data_.position;
		glm::vec3 lightPos = std::static_pointer_cast<Transform>(
			owner()->GetComponent("Transform")
			)->getPosition();
		glm::mat4 proj = glm::ortho(-ortho_width_, ortho_width_, -ortho_width_, ortho_width_, near_, far_);
		glm::mat4 view = glm::lookAt(lightPos, lightPos + data_.direction, std::abs(data_.direction.y)>.99f?glm::vec3(0,0,1):glm::vec3(0,1,0));
		//lightProj = proj;
	//}
	return std::make_tuple(view,proj);
}

std::shared_ptr<DirectionLight> DirectionLight::setDirection(const glm::vec3& dir) {
    checkLogicThread();
	auto candidate=getData();candidate.direction=dir;setData(candidate);
	return std::dynamic_pointer_cast<DirectionLight>(shared_from_this());
}

std::shared_ptr<DirectionLight> DirectionLight::setCastShadow(bool castShadow_) {
    checkLogicThread();
	this->castShadow = castShadow_;dirty=true;invalidate();
	return std::dynamic_pointer_cast<DirectionLight>(shared_from_this());
}

void DirectionLight::loadFromJson(json& json_data) {
    checkLogicThread();
    auto candidate=getData();
	if (json_data.find("color") != json_data.end()) {
		for (int i = 0; i < 3; i++) {
			candidate.color[i] = json_data["color"][i].get<float>();
		}
	}
	if (json_data.find("direction") != json_data.end()) {
		auto& dir = json_data["direction"];
		for (int i = 0; i < 3; i++) {
			candidate.direction[i] = dir[i].get<float>();
		}
	}
    setData(candidate);
}

SpotLight::SpotLight()
{
	Component::name = "SpotLight";
	Light::type = LightType::SPOT;
	Light::castShadow = true;
	Light::dirty = true;

	data_ = {
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

	aspect_ = Light::SHADOW_HEIGHT * 1.0 / Light::SHADOW_WIDTH;
	fov_ = 90.0f;
	near_ = 0.1f;
	far_ = 100.f;
}

SpotLight::~SpotLight()
{

}

tuple<glm::mat4, glm::mat4> SpotLight::getLightTransform() {
    checkLogicThread();
		glm::vec3 lightPos = std::static_pointer_cast<Transform>(
			owner()->GetComponent("Transform")
			)->getPosition();
		glm::mat4 proj = glm::perspective(glm::radians(fov_), aspect_, near_, far_);
		glm::mat4 view = glm::lookAt(lightPos, lightPos+data_.direction, std::abs(data_.direction.y)>.99f?glm::vec3(0,0,1):glm::vec3(0,1,0));
		//lightProj = proj;
	//}
	return std::make_tuple(view, proj);
}

void SpotLight::loadFromJson(json& json_data) {
    checkLogicThread();
    auto candidate=getData();
	if (json_data.find("color") != json_data.end()) {
		for (int i = 0; i < 3; i++) {
			candidate.color[i] = json_data["color"][i].get<float>();
		}
	}
	if (json_data.find("direction") != json_data.end()) {
		auto& dir = json_data["direction"];
		for (int i = 0; i < 3; i++) {
			candidate.direction[i] = dir[i].get<float>();
		}
	}
	if (json_data.find("cutOff") != json_data.end()) {
		candidate.cutOff = json_data["cutOff"].get<float>();
		candidate.cutOff = glm::cos(glm::radians(candidate.cutOff));
	}
	if (json_data.find("outerCutOff") != json_data.end()) {
		candidate.outerCutOff = json_data["outerCutOff"].get<float>();
		candidate.outerCutOff = glm::cos(glm::radians(candidate.outerCutOff));
	}
    setData(candidate);
}
namespace {
void validLightVector(glm::vec3 value,bool color=false) {
    for(int i=0;i<3;i++)if(!std::isfinite(value[i]) || (color && value[i]<0))throw std::invalid_argument("Light values must be finite and colors nonnegative");
}
void validAttenuation(float c,float l,float q) {
    for(auto value:{c,l,q})if(!std::isfinite(value) || value<0)throw std::invalid_argument("Invalid light attenuation");
    if(c+l+q<=0)throw std::invalid_argument("Light attenuation cannot be zero");
}
void validProjection(float n,float f,float a) {
    if(!std::isfinite(n)||!std::isfinite(f)||!std::isfinite(a)||n<=0||f<=n||a<=0)throw std::invalid_argument("Invalid shadow projection");
}
void validPerspective(float n,float f,float fov,float a) {
    validProjection(n,f,a);if(!std::isfinite(fov)||fov<=0||fov>=179)throw std::invalid_argument("Invalid shadow FOV");
}
glm::vec3 validDirection(glm::vec3 value) {
    validLightVector(value);const double length=glm::length(glm::dvec3(value));if(length<1e-6)throw std::invalid_argument("Light direction cannot be zero");return glm::vec3(glm::dvec3(value)/length);
}
}
void PointLight::setData(PointLightData value) {
    checkLogicThread();validLightVector(value.color,true);validLightVector(value.ambient,true);validLightVector(value.diffuse,true);validLightVector(value.specular,true);validAttenuation(value.constant,value.linear,value.quadratic);data_=value;dirty=true;invalidate();
}
void DirectionLight::setData(DirectionLightData value) {
    checkLogicThread();validLightVector(value.color,true);validLightVector(value.ambient,true);validLightVector(value.diffuse,true);validLightVector(value.specular,true);value.direction=validDirection(value.direction);data_=value;dirty=true;invalidate();
}
void SpotLight::setData(SpotLightData value) {
    checkLogicThread();validLightVector(value.color,true);validLightVector(value.ambient,true);validLightVector(value.diffuse,true);validLightVector(value.specular,true);value.direction=validDirection(value.direction);validAttenuation(value.constant,value.linear,value.quadratic);
    if(!std::isfinite(value.cutOff)||!std::isfinite(value.outerCutOff)||value.cutOff>1||value.outerCutOff< -1||value.cutOff<=value.outerCutOff)throw std::invalid_argument("Spot inner cosine must exceed outer cosine in [-1,1]");
    data_=value;dirty=true;invalidate();
}
void PointLight::setColor(glm::vec3 value){auto candidate=getData();candidate.color=value;setData(candidate);}
void DirectionLight::setColor(glm::vec3 value){auto candidate=getData();candidate.color=value;setData(candidate);}
void SpotLight::setColor(glm::vec3 value){auto candidate=getData();candidate.color=value;setData(candidate);}
void SpotLight::setDirection(glm::vec3 value){auto candidate=getData();candidate.direction=value;setData(candidate);}
void SpotLight::setCone(float inner,float outer){auto candidate=getData();candidate.cutOff=inner;candidate.outerCutOff=outer;setData(candidate);}
void PointLight::setShadowProjection(float n,float f,float v,float a){checkLogicThread();validPerspective(n,f,v,a);near_=n;far_=f;fov_=v;aspect_=a;dirty=true;invalidate();}
void SpotLight::setShadowProjection(float n,float f,float v,float a){checkLogicThread();validPerspective(n,f,v,a);near_=n;far_=f;fov_=v;aspect_=a;dirty=true;invalidate();}
void DirectionLight::setShadowProjection(float n,float f,float w){checkLogicThread();validProjection(n,f,w);near_=n;far_=f;ortho_width_=w;dirty=true;invalidate();}
