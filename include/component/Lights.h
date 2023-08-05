#pragma once
#include<glm/glm.hpp>
#include"utils/Shader.h"
#include<glfw/glfw3.h>
#include<vector>
#include<memory>
#include"component/Component.h"
#include<tuple>
#include<json/json.hpp>
using json = nlohmann::json;

class Texture;
using std::tuple;
using std::vector;
using std::shared_ptr;

enum class LightType {
	POINT,
	DIRECTIONAL,
	SPOT
};
class Light :public Component, public std::enable_shared_from_this<Light> {
public:
	Light();
	virtual ~Light();
	void setDirtyFlag(bool dirty);

public:

	bool castShadow;
	bool enabled;
	enum LightType type; 
	static const int SHADOW_WIDTH = 512;
	static const int SHADOW_HEIGHT = 512; 
	
	bool dirty; //if position of light changes, this flag turns true
	// light 是否 dirty 要交给 prepare light data 统一设置
};

//enum class POINTLIGHT {
	//SINGLE,
	//CUBEMAP
//};
struct PointLightData{
	glm::vec3 color; 
	//glm::vec3 position;// get position from transform

	// not used
	glm::vec3 ambient; 
	glm::vec3 diffuse; 
	glm::vec3 specular; 
	float constant;
	float linear; 
	float quadratic; 
};
class PointLight: public Light{

public:
	PointLight();
	virtual ~PointLight();
	//void initVertexObject();
	tuple<glm::mat4,glm::mat4> getLightTransform(int face);
	std::shared_ptr<PointLight> setCastShadow(bool castShadow);
	virtual void loadFromJson(json& data)override;

public:
	PointLightData data; 

	//POINTLIGHT mode; 

	//std::vector <glm::mat4> lightTransforms; // 6 faces, 6 view transforms (for shadow map) 

	float near; 
	float far; 
	float fov; 
	float aspect; 

	// shadow mapping texture;
	std::shared_ptr<Texture> shadowTex;
};

struct DirectionLightData {
	glm::vec3 color; 
	//glm::vec3 position; // get position from transform
	glm::vec3 direction;

	// not used
	glm::vec3 ambient; 
	glm::vec3 diffuse;
	glm::vec3 specular; 
};

class DirectionLight : public Light {
public:
	DirectionLightData data;

	float near; 
	float far;
	float ortho_width;

	
	//glm::mat4 lightView; 
	//glm::mat4 lightProj;

	// shadow mapping texture
	std::shared_ptr<Texture> shadowTex;

public:
	DirectionLight();
	~DirectionLight();
	std::shared_ptr<DirectionLight> setDirection(const glm::vec3& dir);
	std::shared_ptr<DirectionLight> setCastShadow(bool castShadow);
	tuple<glm::mat4,glm::mat4> getLightTransform();
	virtual void loadFromJson(json& data)override;
};


struct SpotLightData {
	glm::vec3 color;
	//glm::vec3 position; // get position from transform
	glm::vec3 direction;

	float cutOff;
	float outerCutOff;

	float constant;
	float linear;
	float quadratic;
	// not used
	glm::vec3 ambient;
	glm::vec3 diffuse;
	glm::vec3 specular;
};

class SpotLight : public Light {
public:
	SpotLightData data;

	float near;
	float far;
	float fov;
	float aspect;

	//glm::mat4 lightView; 
	//glm::mat4 lightProj;

	// shadow mapping texture
	std::shared_ptr<Texture> depthMap;
	std::shared_ptr<Texture> normalMap;
	std::shared_ptr<Texture> worldPosMap;
	std::shared_ptr<Texture> fluxMap;

public:
	SpotLight();
	~SpotLight();
	tuple<glm::mat4, glm::mat4> getLightTransform();
	virtual void loadFromJson(json& data)override;
};



