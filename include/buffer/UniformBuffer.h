#pragma once
#include"buffer.h"
#include<iostream>

class UniformBuffer:public Buffer {
public:
	unsigned int UBO;
	int size; 
	int binding;
	bool dirty; // dirty flag, reset shader's ubo bindings if dirty
	//this flag only indicates when to reset UBO bindings in shaders, not UBO data.

public:
	UniformBuffer(int size);
	~UniformBuffer();

	virtual void bindBuffer();
	virtual void unbindBuffer();
	virtual void setBinding(int binding);
	void setDirtyFlag(bool flag);
};

