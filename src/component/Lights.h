#pragma once
#include<glm/glm.hpp>
#include<shader/Shader.h>
#include<glfw/glfw3.h>
#include<memory>

class LightBase {

	virtual void setShader(std::shared_ptr<Shader> shader) {};

};

class PointLight {
public:
	PointLight();
	~PointLight();

	glm::mat4 model; 
	glm::vec3 lightPos; 
	glm::vec3 size; 
	GLuint VAO, VBO;

	void initVertexObject();
	void render(Shader& shader); 
};

