#pragma once

class RenderBuffer {
public:
	RenderBuffer();
	~RenderBuffer();

	void genBuffer(int width, int height);
	void bindBuffer();
	void unbindBuffer();

public:
	unsigned int rbo;
};
