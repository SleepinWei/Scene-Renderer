#pragma once

class SSBO {
	// shader storage buffer
public:
	SSBO(int size) {};
	~SSBO() {};
	void bindBuffer() {};
	void setBinding() {};

public:
	unsigned int ssbo; 
	int size; 
	int binding; 
};
