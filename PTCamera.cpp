#include"PTCamera.h"

PT::Camera::Camera() {
	aspect_ratio = 16.0 / 9.0;
	viewport_height = 2.0;
	viewport_width = aspect_ratio * viewport_height;
	focal_length = 1.0;

	origin = vec3(0, 0, 0);
	horizontal = vec3(viewport_width, 0.0, 0.0);
	vertical = vec3(0.0, viewport_height, 0.0);
	lower_left_corner = origin - horizontal / 2 - vertical / 2 - vec3(0, 0, focal_length);
}

PT::Ray PT::Camera::get_ray(double u, double v){
	return Ray(origin, lower_left_corner + u * horizontal + v * vertical - origin);
}
