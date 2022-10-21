#include<glad/glad.h>
#include<glfw/glfw3.h>
#include"../renderer/Texture.h"
#include"../buffer/FrameBuffer.h"

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

void FrameBuffer::bindTexture(const std::shared_ptr<Texture>& tex,GLenum attachment,TextureType type, int face_) {
	switch (type)
	{
	case TextureType::CUBEMAP:
	{
		GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + face_;
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, face, tex->id, 0);
	}
		break;
	case TextureType::FLAT: {
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, tex->id, 0);
	}
		break;
	default:
		break;
	}
}