#pragma once
#include"buffer.h"
#include<memory>

class Texture;
class ImageTexture:public Buffer {
public:
	ImageTexture();
	~ImageTexture();

	void genImageTexture(unsigned int TexType,unsigned int channelType,int width,int height);
	int getHeight();
	int getWidth();
	unsigned int getTexture();// get texture id in opengl
	virtual void bindBuffer() override;
	virtual void setBinding(int binding) override;
	virtual void unbindBuffer() override;
public:
	std::shared_ptr<Texture> tex;
	int binding; 
};
