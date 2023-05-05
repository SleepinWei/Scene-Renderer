#include<glad/glad.h>
#include"buffer/ImageTexture.h"
#include"renderer/Texture.h"

ImageTexture::ImageTexture() {
	tex = nullptr;
	binding = 0;
}

ImageTexture::~ImageTexture() {

}

void ImageTexture::genImageTexture(unsigned int DataType, unsigned int channelType, int width, int height) {
	tex = std::make_shared<Texture>()->genTexture(DataType,channelType,width,height);
}

void ImageTexture::bindBuffer() {
	glBindTexture(GL_TEXTURE_2D, tex->id);
}

void ImageTexture::setBinding(int binding) {
	this->binding = binding;
	glBindImageTexture(binding, tex->id, 0, GL_FALSE, 0, GL_READ_WRITE,GL_RGBA32F);
}

int ImageTexture::getHeight() {
	if(tex)
		return tex->height;
}

int ImageTexture::getWidth() {
	if (tex)
		return tex->width;
}

unsigned int ImageTexture::getTexture() {
	if(tex)
		return tex->id;
}

void ImageTexture::unbindBuffer() {
	glBindTexture(GL_TEXTURE_2D, 0);
}
