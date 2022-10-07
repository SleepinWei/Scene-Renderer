#pragma once

class SSBO {
	// shader storage buffer
public:
	SSBO();
	SSBO(int size);
	~SSBO() ;
	void bindBuffer();
	void unbindBuffer();
	void setBinding(int binding);

public:
	unsigned int ssbo; 
	int size; 
	int binding; 

	bool dirty; 
};
