#pragma once
#include<iostream>

class UniformBuffer {
public:
	unsigned int UBO;
	int size; 
	int binding;
	bool dirty; // dirty flag, reset shader's ubo bindings if dirty

public:
	UniformBuffer(int size);
	~UniformBuffer();

	void bindBuffer();
	void setBinding(int binding);
	void setDirtyFlag(bool flag);
};

