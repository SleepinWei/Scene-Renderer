#include"UniformBuffer.h"
#include<glad/glad.h>
#include<glfw/glfw3.h>

UniformBuffer::UniformBuffer(int size) {
	this->size = size;
	glGenBuffers(1, &UBO);
	glBindBuffer(GL_UNIFORM_BUFFER, UBO);
	glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_STATIC_DRAW);
}



UniformBuffer::~UniformBuffer() {
	if (UBO) {
		glDeleteBuffers(1,&UBO);
	}
}

void UniformBuffer::bindBuffer() {
	glBindBuffer(GL_UNIFORM_BUFFER, UBO);
}

void UniformBuffer::setBinding(int binding) {
	if(UBO){
		this->binding = binding;
		glBindBufferRange(GL_UNIFORM_BUFFER, binding, UBO, 0, size);
	}
	else {
		std::cout << "In UniformBuffer:setBinding, no UBO created" << '\n';
	}
}