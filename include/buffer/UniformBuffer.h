#pragma once
#include "rhi/Device.h"

// Transitional renderer wrapper; bindings migrate to BindingSet next.
class UniformBuffer {
public:
	int size; 
	int binding;
	bool dirty; // dirty flag, reset shader's ubo bindings if dirty
	//this flag only indicates when to reset UBO bindings in shaders, not UBO data.

public:
	explicit UniformBuffer(int size);
	~UniformBuffer();
	UniformBuffer(const UniformBuffer&) = delete;
	UniformBuffer& operator=(const UniformBuffer&) = delete;
	void write(size_t offset, size_t bytes, const void* data);
	void setBinding(int binding);
	void setDirtyFlag(bool flag) { dirty = flag; }
private:
	std::shared_ptr<rhi::Device> owner_;
	rhi::BufferHandle handle_;
};
