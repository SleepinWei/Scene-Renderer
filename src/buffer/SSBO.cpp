#include<glad/glad.h>
#include<glfw/glfw3.h>

#include"buffer/SSBO.h"

SSBO::SSBO() {
	ssbo = 0; 
	size = 0; 
	binding = -1;
	
	dirty = true;
}

SSBO::SSBO(int size) {
	binding = -1; 
	dirty = true; 

	this->size = size; 
	glGenBuffers(1, &ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, size, NULL, GL_DYNAMIC_COPY);
}

SSBO::~SSBO() {
	if (ssbo) {
		glDeleteBuffers(1, &ssbo);
	}
}

void SSBO::bindBuffer() {
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
}

void SSBO::unbindBuffer() {
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
}

void SSBO::setBinding(int binding) {
	this->binding = binding;
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, ssbo);
}
