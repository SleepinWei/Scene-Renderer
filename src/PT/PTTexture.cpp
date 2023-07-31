// #define STB_IMAGE_IMPLEMENTATION
#include"PT/PTTexture.h"
#include<stb/stb_image.h>
#include"PT/PTrandom.h"
using namespace PT;

ImageTexture::ImageTexture() {
	data = nullptr;
	width = height = 0;
	bytes_per_scanline = 0;
	path = "";
}

ImageTexture::ImageTexture(const std::string& path) {
	this->path = path;
	
	auto channels = bytes_per_pixel;

	stbi_set_flip_vertically_on_load(true);
	stbi_load(path.c_str(), &width, &height, &channels, channels);

	bytes_per_scanline = width * bytes_per_pixel;
}
ImageTexture::~ImageTexture() {
	if (data) {
		stbi_image_free(data);
	}
}

vec3 ImageTexture::value(float u, float v, const vec3& p)const {
    // If we have no texture data, then return solid cyan as a debugging aid.
    if (data == nullptr)
        return vec3(0, 1, 1);

    // Clamp input texture coordinates to [0,1] x [1,0]
    u = clamp(u, 0.0, 1.0);
    v = clamp(v, 0.0, 1.0);  // Flip V to image coordinates

    auto i = static_cast<int>(u * (width-1));
    auto j = static_cast<int>(v * (height-1));

    const auto color_scale = 1.0 / 255.0;
    int index = j * width + i;
    auto pixel = data + index * bytes_per_pixel;

    return vec3(color_scale * pixel[0], color_scale * pixel[1], color_scale * pixel[2]);
}
