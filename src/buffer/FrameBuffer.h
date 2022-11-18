#pragma once
#include<iostream>
#include"buffer.h"

typedef unsigned int GLenum;

class Texture;

class FrameBuffer:public Buffer {
public:
	unsigned int FBO;
	bool dirty; 

public:
	FrameBuffer();
	~FrameBuffer();

	virtual void bindBuffer();
	virtual void unbindBuffer();
	
	void bindTexture(const std::shared_ptr<Texture>& tex,GLenum attachment,GLenum textarget);
};
