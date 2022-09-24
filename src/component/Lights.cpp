#include"Lights.h"
#include<glm/gtc/matrix_transform.hpp>
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
