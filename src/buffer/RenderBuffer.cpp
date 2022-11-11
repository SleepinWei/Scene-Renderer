#include<glad/glad.h>
#include<glfw/glfw3.h>
#include"RenderBuffer.h"

RenderBuffer::RenderBuffer() {
	glGenRenderbuffers(1, &rbo);
}

RenderBuffer::~RenderBuffer() {
	glDeleteRenderbuffers(1, &rbo);
}

void RenderBuffer::bindBuffer() {
	glBindRenderbuffer(GL_RENDERBUFFER, rbo);
}

void RenderBuffer::unbindBuffer() {
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void RenderBuffer::genBuffer(int width, int height) {
	bindBuffer();
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
}