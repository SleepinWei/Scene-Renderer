#pragma once
#include<vector>
//#include<shader/Shader.h>
#include<glm/glm.hpp>
#include<glfw/glfw3.h>
#include<shader/Shader.h>
#include"Material.h"
#include"global.h"

class Sphere :public Renderable{
public:
	std::vector<float> m_vertices; 
	std::vector<float> m_normals; 
	std::vector<float> m_TexCoords;
	std::vector<GLuint> m_indices;

	glm::mat4 model; 
	glm::vec3 position;
	glm::vec3 scale; 

	std::shared_ptr<Material> material; 

	int pointNum; 

	GLuint VAO, VBO,EBO;

public:
	Sphere(enum class TEX_TYPE type);
	void initGeometry();
	void initVertexObject();
	void addMaterial(enum TEX_TYPE mType);
	void changeGeometry(int pointNum);
	virtual void render() override;
	virtual void registerShader(ShaderType type) override;
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
