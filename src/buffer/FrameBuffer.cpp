#include<glad/glad.h>
#include<glfw/glfw3.h>
#include"renderer/Texture.h"
#include"buffer/FrameBuffer.h"

FrameBuffer::FrameBuffer() {
	glGenFramebuffers(1, &FBO);
	dirty = true;
}

FrameBuffer::~FrameBuffer() {
	glDeleteFramebuffers(1, &FBO);
}

void FrameBuffer::bindBuffer() {
	glBindFramebuffer(GL_FRAMEBUFFER, FBO);
}

void FrameBuffer::unbindBuffer() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffer::bindTexture(const std::shared_ptr<Texture>& tex,GLenum attachment,GLenum textarget) {
	glBindFramebuffer(GL_FRAMEBUFFER, this->FBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, textarget, tex->id, 0);
}

//to generate a series of maps to implementing CSM
void FrameBuffer::bindShadowTexture(const std::shared_ptr<Texture>& tex, GLenum attachment)
{
	glBindFramebuffer(GL_FRAMEBUFFER, this->FBO);
	glFramebufferTexture(GL_FRAMEBUFFER, attachment, tex->id, 0);
}
