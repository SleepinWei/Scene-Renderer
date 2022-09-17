#include<glad/glad.h>
#include"Object.h"
const float PI = 3.1415926f;
#include<glm/gtc/matrix_transform.hpp>
#include<iostream>

Sphere::Sphere(enum class TEX_TYPE type) {
	pointNum = 64; 
	initGeometry();
	initVertexObject();
	material = resourceManager.registerResource(type);
	model = glm::mat4(1); 
	position = glm::vec3(0,3,0);
	scale = glm::vec3(0.5, 0.5, 0.5);
}

void Sphere::changeGeometry(int pointNum) {
	this->pointNum = pointNum;
	destroy();
	initGeometry();
	initVertexObject();
}

void Sphere::initGeometry() {
	float delta = 2 * PI * 1.0 / pointNum; // delta of angle
	float R = 1;
	int overallPointNum = (pointNum/2-1) * (pointNum + 1) + 2;
	std::vector<float>(overallPointNum * 3,0).swap(m_vertices);
	std::vector<float>(overallPointNum * 3, 0).swap(m_normals);
	std::vector<float>(overallPointNum * 2, 0).swap(m_TexCoords);

	int overallTriangles = pointNum * 2 * (pointNum / 2 - 1);
	std::vector<GLuint>(overallTriangles * 3,0).swap(m_indices);

	m_vertices[0] = 0;
	m_vertices[1] = R; 
	m_vertices[2] = 0;
	m_TexCoords[0] = 0.5; 
	m_TexCoords[1] = 0;
	int index = 1;
	for (int i = 0; i < pointNum / 2 - 1; i++) {
		for (int j = 0; j <= pointNum; j++) {
			float theta = (i + 1) * delta;
			float phi = j * delta;
			m_vertices[3 * index] = R * sin(theta) * sin(phi);
			m_vertices[3 * index + 1] = R * cos(theta) ;
			m_vertices[3 * index + 2] = R * sin(theta) * cos(phi);
			m_TexCoords[2 * index] = j * 1.0 / (pointNum); 
			m_TexCoords[2 * index + 1] = (i+1) * 1.0 / (pointNum /2);
			++index;
		}
	}
	m_vertices[3 * index] = 0;
	m_vertices[3 * index + 1] = -R;
	m_vertices[3 * index + 2] = 0;
	m_TexCoords[2 * index] = 0.5; 
	m_TexCoords[2 * index + 1] = 1; 
	// set indices; 
	index = 0; 
	for (int i = 0; i < pointNum; i++) {
		m_indices[index++] = 0; 
		m_indices[index++] = i + 1;
		m_indices[index++] = i + 2; 
	}
	for (int i = 0; i < pointNum/2-2; i++) {
		int basicPointIndex = i * (pointNum+1) + 1;
		for (int j = 0; j < pointNum; j++) {
			int curPoint = basicPointIndex + j; 
			//int nextPoint =(j != pointNum -1 ? curPoint + 1 : basicPointIndex); 
			int nextPoint = curPoint + 1;
			m_indices[index++] = curPoint;
			m_indices[index++] = curPoint + pointNum + 1; 
			m_indices[index++] = nextPoint; 
			m_indices[index++] = curPoint + pointNum + 1; 
			m_indices[index++] = nextPoint + pointNum + 1;
			m_indices[index++] = nextPoint;
		}
	}
	int basicPointIndex = (pointNum / 2 - 2) * (pointNum+1) + 1;
	for (int i = 0; i < pointNum; i++) {
		int curPoint = basicPointIndex + i;
		//int nextPoint = (i != pointNum - 1 ? curPoint + 1 : basicPointIndex);
		int nextPoint = curPoint + 1;
		m_indices[index++] = curPoint;
		m_indices[index++] = overallPointNum - 1;
		m_indices[index++] = nextPoint;
	}

	//normal 
	for (int i = 0; i < m_normals.size(); i++) {
		m_normals[i] = m_vertices[i];
	}
	//std::cout << m_indices.size() << '\n';
	//for (int i = 0; i < overallPointNum; i++) {
	//	std::cout << m_normals[3 * i] << ' ' << m_normals[3 * i + 1] << ' ' << m_normals[3 * i + 2] << '\n';
	//	std::cout << m_TexCoords[2 * i] << ' ' << m_TexCoords[2 * i + 1] << '\n';
	//}
	//std::cout << '\n';

	//std::cout << (index == overallTriangles * 3 ? "Yes" : "No") << '\n';
}

void Sphere::addMaterial(TEX_TYPE mType) {
	material = resourceManager.registerResource(mType);
}

void Sphere::initVertexObject() {
	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1,&VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, (m_vertices.size()+m_normals.size() + m_TexCoords.size()) * sizeof(float), nullptr, GL_STATIC_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, 0, m_vertices.size() * sizeof(float), &m_vertices[0]);
	glBufferSubData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(float), m_normals.size() * sizeof(float), &m_normals[0]);
	glBufferSubData(GL_ARRAY_BUFFER, (m_vertices.size() + m_normals.size()) * sizeof(float), m_TexCoords.size() * sizeof(float), &m_TexCoords[0]);

	glGenBuffers(1, &EBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(GLuint), &m_indices[0], GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)(m_vertices.size() * sizeof(float)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)((m_vertices.size() + m_normals.size()) * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void Sphere::render() {
	std::shared_ptr<Material> M_Material = material;
	M_Material->setBeginIndex(0);
	M_Material->render();
	
	model = glm::mat4(1);
	model = glm::translate(model, position);
	model = glm::scale(model, scale);

	shader->setMat4("model", model);

	glBindVertexArray(VAO);
	glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, 0);
	//glDrawArrays(GL_TRIANGLES, 0,m_vertices.size());
	glBindVertexArray(0);
}

void Sphere::registerShader(ShaderType type){
	shader = renderManager.registerShader(type);
	if (material) {
		material->registerShader(type);
	}
}

void Sphere::destroy() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
}

Sphere::~Sphere() {
	destroy();
}
Plane::Plane() {
	initGeometry();
	initVertexObject();
	model = glm::mat4(1); 
	model = glm::scale(model, glm::vec3(3));
}


Plane::~Plane() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
}

void Plane::initGeometry() {

	std::vector<float> vertexTemp{
		1.0f, 0.0f, 1.0f,
		1.0f, 0.0f, -1.0f,
		-1.0f, 0.0f, -1.0f,
		-1.0f, 0.0f, 1.0f
	};
	vertexTemp.swap(m_vertices);
	std::vector<GLuint> indexTemp{
		0, 1, 2,
		0, 2, 3
	};
	indexTemp.swap(m_indices);

	std::vector<float>normalTemp{
		0.0f,1.0f,0.0f,
		0.0f,1.0f,0.0f,
		0.0f,1.0f,0.0f,
		0.0f,1.0f,0.0f
	};
	normalTemp.swap(m_normals);
}

void Plane::initVertexObject() {
	glGenBuffers(1, &VBO);
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &EBO);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, (m_vertices.size() + m_normals.size()) * sizeof(float), nullptr, GL_STATIC_DRAW);
	
	glBufferSubData(GL_ARRAY_BUFFER, 0, m_vertices.size() * sizeof(float), &m_vertices[0]);
	glBufferSubData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(float), m_normals.size() * sizeof(float), &m_normals[0]);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(GLuint), &m_indices[0], GL_STATIC_DRAW);
	
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)(m_vertices.size() *sizeof(float)));
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void Plane:: render(Shader& shader) {
	shader.use();
	shader.setMat4("model", model);
	glBindVertexArray(VAO);
	glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}

PointLight::PointLight() {
	lightPos = glm::vec3(0, 5, 0);
	model = glm::mat4(1); 
	initVertexObject();
}

void PointLight::initVertexObject() {
	float vertices[] = { 0,0,0 };

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
}

void PointLight::render(Shader& shader) {
	model = glm::mat4(1);
	model = glm::translate(model, lightPos);
	shader.setMat4("model", model);

	glBindVertexArray(VAO);
	glDrawArrays(GL_POINTS, 0, 1);
}

PointLight::~PointLight() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
}
