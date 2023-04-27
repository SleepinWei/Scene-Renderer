#pragma once
#include<glm/glm.hpp>
#include<string>
using glm::vec3;

namespace PT {
	class Texture {
	public:
		virtual vec3 value(float u, float v, const vec3& p) const = 0;
	};

	class SolidColor :public Texture {
	public:
		SolidColor() {};
		SolidColor(vec3 c) :color_value(c) {};

		SolidColor(float r, float g, float b)
			:SolidColor(vec3(r, g, b)) {}

		virtual vec3 value(float u, float v, const vec3& p)const override {
			return color_value;
		}

	public:
		vec3 color_value;
	};

	class ImageTexture :public Texture {
	public:
		ImageTexture();
		ImageTexture(const std::string& path);
		~ImageTexture();

		virtual vec3 value(float u, float v, const vec3& p)const override;
	public:
		unsigned char* data;
		const static int bytes_per_pixel = 3;
		int width;
		int height;
		int bytes_per_scanline;
		std::string path;
	};
}
