#pragma once
#include<glm/glm.hpp>
#include"../utils/Shader.h"
#include<glfw/glfw3.h>
#include<vector>
#include<memory>
#include"../component/Component.h"

enum class LightType {
	POINT,
	DIRECTIONAL	
};
class Light :public Component, public std::enable_shared_from_this<Light> {
public:
	Light();
	virtual ~Light();
	void setDirtyFlag(bool dirty);

public:

	bool castShadow;
	enum LightType type; 
	const int SHADOW_WIDTH = 1024;
	const int SHADOW_HEIGHT = 1024; 
	
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
	std::vector<glm::mat4> getLightMatrix();

public:
	PointLightData data; 

	//POINTLIGHT mode; 

	std::vector <glm::mat4> lightTransforms; // 6 faces, 6 view transforms (for shadow map) 

	float near; 
	float far; 
	float fov; 
	float aspect; 
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

	glm::mat4 lightTransforms; 

public:
	DirectionLight();
	~DirectionLight();
	std::shared_ptr<DirectionLight> setDirection(const glm::vec3& dir);
	glm::mat4 getLightMatrix(); 
};

