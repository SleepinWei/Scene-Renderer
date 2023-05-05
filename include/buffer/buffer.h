#pragma once

class Buffer {
public:
	Buffer() = default;
	virtual ~Buffer() = default;

	virtual void bindBuffer() {};
	virtual void unbindBuffer() {};
	virtual void setBinding(int binding) {};
};
