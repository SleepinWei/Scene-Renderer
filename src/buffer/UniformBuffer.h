#pragma once
#include<iostream>

class UniformBuffer {
public:
	unsigned int UBO;
	int size; 
	int binding;

public:
	UniformBuffer(int size);
	~UniformBuffer();

	void bindBuffer();
	void setBinding(int binding);
};

