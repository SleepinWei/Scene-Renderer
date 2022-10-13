#pragma once
#include"buffer.h"

class SSBO :public Buffer{
	// shader storage buffer
public:
	SSBO();
	SSBO(int size);
	~SSBO() ;
	virtual void bindBuffer() override;
	virtual void unbindBuffer() override;
	virtual void setBinding(int binding) override;

public:
	unsigned int ssbo; 
	int size; 
	int binding; 

	bool dirty; 
};
