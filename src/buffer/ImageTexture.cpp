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
	glBindTexture(GL_TEXTURE_2D, tex->gpuId());
}

void ImageTexture::setBinding(int binding) {
	this->binding = binding;
	glBindImageTexture(binding, tex->gpuId(), 0, GL_FALSE, 0, GL_READ_WRITE,GL_RGBA32F);
}

int ImageTexture::getHeight() {
	if(tex)
		return tex->getHeight();
}

int ImageTexture::getWidth() {
	if (tex)
		return tex->getWidth();
}

unsigned int ImageTexture::getTexture() {
	if(tex)
		return tex->gpuId();
}

void ImageTexture::unbindBuffer() {
	glBindTexture(GL_TEXTURE_2D, 0);
}
