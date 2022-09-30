#include<glad/glad.h>
#include<glfw/glfw3.h>
#include"../renderer/Texture.h"
#include"../buffer/FrameBuffer.h"

FrameBuffer::FrameBuffer() {
	glGenFramebuffers(1, &FBO);
}

FrameBuffer::~FrameBuffer() {

}

void FrameBuffer::bindBuffer() {
	//TODO:
}