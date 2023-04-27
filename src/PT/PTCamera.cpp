#include"PT/PTCamera.h"

using glm::radians;
using namespace PT;

PT::Camera::Camera(
	vec3 lookfrom, vec3 lookat,
	vec3 vup,
	float fov,
	int width, int height
) {
	focal_length = 1.0;
	this->width = width;
	this->height = height;

	this->aspect_ratio = width * 1.0f / height;
	auto theta = radians(fov);
	auto h = tanf(theta / 2) * focal_length;
	viewport_height = 2.0 * h;
	viewport_width = aspect_ratio * viewport_height;

	vec3 w = normalize(lookfrom - lookat);
	vec3 u = normalize(cross(vup, w));
	vec3 v = cross(w, u);

	origin = lookfrom;
	horizontal = viewport_width * u;
	vertical = viewport_height * v;
	lower_left_corner = origin - horizontal / 2.0f - vertical / 2.0f - w;
}

PT::Ray PT::Camera::get_ray(float u, float v)const{
	return Ray(origin, lower_left_corner + u * horizontal + v * vertical - origin);
}
