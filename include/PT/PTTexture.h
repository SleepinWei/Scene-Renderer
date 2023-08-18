#pragma once
#include <glm/glm.hpp>
#include <string>
using glm::vec3;

class PTTexture
{
public:
	virtual vec3 value(float u, float v, const vec3 &p) const = 0;
};

class SolidColor : public PTTexture
{
public:
	SolidColor(){};
	SolidColor(vec3 c) : color_value(c){};

	SolidColor(float r, float g, float b)
		: SolidColor(vec3(r, g, b)) {}

	virtual vec3 value(float u, float v, const vec3 &p) const override
	{
		return color_value;
	}

public:
	vec3 color_value;
};

class PTImageTexture : public PTTexture
{
public:
	PTImageTexture();
	PTImageTexture(const std::string &path);
	~PTImageTexture();

	virtual vec3 value(float u, float v, const vec3 &p) const override;

public:
	unsigned char *data;
	const static int bytes_per_pixel = 3;
	int width;
	int height;
	int bytes_per_scanline;
	std::string path;
};
