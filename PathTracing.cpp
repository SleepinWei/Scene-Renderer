#include"PathTracing.h"
#include"PTVec.h"
#include"PTCamera.h"
#include<iostream>
#include"PTrandom.h"
#include"PTMaterial.h"
#include"hittable.h"

class PT::Material;


PT::vec3 TBN(PT::vec3 v, PT::vec3& T, PT::vec3& B, PT::vec3& N) {
	return PT::vec3(
		v.x * T.x + v.y * B.x + v.z * N.x,
		v.x * T.y + v.y * B.y + v.z * N.y,
		v.x * T.z + v.y * B.z + v.z * N.z
	);
}


PT::vec3 PT::rayColor(Ray r,hittable& world,int depth) {
	hitRecord rec;

	if (depth <= 0) {
		return vec3(0, 0, 0);
	}

	bool isHit = world.hit(r,0,infinity,rec);
	if (isHit) {
		Ray scattered;
		vec3 attenuation;
		//new_ray_dir = TBN(new_ray_dir, rec.T, rec.B, rec.normal);
		if (rec.mat_ptr->scatter(r, rec, attenuation, scattered)) {
			vec3 color = rayColor(scattered, world, depth - 1);
			return attenuation * color;
		}
		return vec3(0, 0, 0);
	}
	vec3 unit_direction = normalize(r.dir);
	auto t = 0.5 * (unit_direction.y + 1.0);
	return (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
}



void write_color(PT::vec3 color, int samples_per_pixel) {
	auto r = color.x; 
	auto g = color.y;
	auto b = color.z;

	auto scale = 1.0 / samples_per_pixel;

	r = sqrt(scale * r);
	g = sqrt(scale * g);
	b = sqrt(scale * b);

	std::cout << int(256 * clamp(r, 0.0, 0.999)) << ' '
		<< int(256 * clamp(g, 0.0, 0.999)) << ' '
		<< int(256 * clamp(b, 0.0, 0.999)) << '\n';
}
void PT::render() {
	freopen("./out.ppm", "w", stdout);

	// image plane  
	Camera camera;
	int h = 450;
	int w= int(camera.aspect_ratio * h);

	// world 
	hittable_list world;

	auto material_ground = Lambertian(vec3(0.8, 0.8, 0.0));
	auto material_center = Lambertian(vec3(0.7, 0.3, 0.3));
	auto material_left = Metal(vec3(0.8, 0.8, 0.8));
	auto material_right = Metal(vec3(0.8, 0.6, 0.2));

	Sphere s1(vec3(0.0, -100.5, -1.0), 100.0, &material_ground);
	Sphere s2(vec3(0.0, 0.0, -1.0), 0.5, &material_center);
	Sphere s3(vec3(-1.0, 0.0, -1.0), 0.5, &material_left);
	Sphere s4(vec3(1.0, 0.0, -1.0), 0.5, &material_right);
	world.add(&s1);
	world.add(&s2);
	world.add(&s3);
	world.add(&s4);

	int samples_per_pixel = 20;
	int max_depth = 10;

	std::cout << "P3\n" << w << ' ' << h << "\n255\n";

	for (int j = h-1; j >=0; j--) {
		std::cerr << "Progress: " << (h-1-j) *1.0/ (h-1) *100 << "%\n";
		for (int i = 0; i < w; i++) {
			vec3 color(0, 0, 0);
			for (int s = 0; s < samples_per_pixel; s++) {
				auto u = (i + random_double()) / (w - 1);
				auto v = (j + random_double()) / (h - 1);
				Ray r = camera.get_ray(u, v);
				color = color + rayColor(r, world,max_depth);
			}
			write_color(color, samples_per_pixel);
		}
	}
	fclose(stdout);
}

