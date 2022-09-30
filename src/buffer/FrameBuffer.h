#pragma once
#include<iostream>

class Texture;
class FrameBuffer {
public:
	unsigned int FBO;
	//bool dirty; 
	std::shared_ptr<Texture> tex;

public:
	FrameBuffer();
	~FrameBuffer();

	void bindBuffer();
};
