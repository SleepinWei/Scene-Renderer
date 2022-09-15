#pragma once
#include<vector>
//#include<shader/Shader.h>
#include<glm/glm.hpp>
#include<glfw/glfw3.h>
#include<shader/Shader.h>
#include"Material.h"


class Object {
public:
	//functions
	// variables
};

class Sphere {
public:
	std::vector<float> m_vertices; 
	std::vector<float> m_normals; 
	std::vector<float> m_TexCoords;
	std::vector<GLuint> m_indices;

	glm::mat4 model; 
	glm::vec3 position;
	glm::vec3 scale; 

	PBRMaterial* material; 

	int pointNum; 

	GLuint VAO, VBO,EBO;

	Sphere(std::string materialPath);
	void initGeometry();
	void initVertexObject();
	void initMaterial(const std::string& path);
	void changeGeometry(int pointNum);
	void render(Shader& shader);
	void destroy();
	~Sphere();
};

class Plane {
public:
	std::vector<float> m_vertices;
	std::vector<GLuint> m_indices;
	std::vector<float> m_normals;
	GLuint VAO, VBO,EBO;

	glm::mat4 model; 

	Plane();
	~Plane();

	void initGeometry();
	void initVertexObject(); 
	void render(Shader& shader);
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
