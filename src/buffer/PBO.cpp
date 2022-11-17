#include<glad/glad.h>
#include"PBO.h"

PixelBuffer::PixelBuffer() {
	id = 0;
	glGenBuffers(1, &id);
	dirty = true;
}

PixelBuffer::~PixelBuffer() {
	glDeleteBuffers(1, &id);
}

void PixelBuffer::bindBuffer() {
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, id);
}

void PixelBuffer::unbindBuffer() {
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}
