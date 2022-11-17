#pragma once
#include"buffer.h"

class PixelBuffer:public Buffer{
public:
	PixelBuffer();
	~PixelBuffer();

	virtual void bindBuffer()override;
	virtual void unbindBuffer()override;
	
	bool dirty;

public:
	unsigned int id;
};
