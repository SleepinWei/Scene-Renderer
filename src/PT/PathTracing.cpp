#define _CRT_SECURE_NO_WARNINGS
#include"PathTracing.h"
//#include"PTVec.h"
#include"PTCamera.h"
#include<memory>
#include<glm/glm.hpp>
#include<iostream>
#include"PTrandom.h"
#include"PTMaterial.h"
#include"hittable.h"
#include"PTTexture.h"
#include"hittable/Rect.h"

using namespace PT;
class PT::Material;
using std::make_shared;
using std::shared_ptr;

//PT::vec3 TBN(PT::vec3 v, PT::vec3& T, PT::vec3& B, PT::vec3& N) {
//	return PT::vec3(
//		v.x * T.x + v.y * B.x + v.z * N.x,
//		v.x * T.y + v.y * B.y + v.z * N.y,
//		v.x * T.z + v.y * B.z + v.z * N.z
//	);
//}


vec3 PT::rayColor(const Ray& r,const hittable& world,int depth) {
	hitRecord rec;

	if (depth <= 0) {
		return vec3(0, 0, 0);
	}

	bool isHit = world.hit(r,0.001,infinity,rec);
	if (!isHit) {
		return background;
	}
	Ray scattered;
	vec3 attenuation;
	vec3 emitted = rec.mat_ptr->emitted(rec.u, rec.v, rec.p);

	bool hasScatter = rec.mat_ptr->scatter(r, rec, attenuation, scattered);
	if (!hasScatter) {
		return emitted;
	}

	vec3 color = rayColor(scattered, world, depth - 1);
	vec3 finalColor = emitted + attenuation * color;
	return finalColor;
}

void write_color(const vec3& color, int samples_per_pixel) {
	auto r = color.x; 
	auto g = color.y;
	auto b = color.z;

	auto scale = 1.0 / samples_per_pixel;

	r = sqrt(scale * r);
	g = sqrt(scale * g);
	b = sqrt(scale * b);
	//r = scale * r;
	//g = scale * g;
	//b = scale * b;

	std::cout << int(256 * clamp(r, 0.0, 0.999)) << ' '
		<< int(256 * clamp(g, 0.0, 0.999)) << ' '
		<< int(256 * clamp(b, 0.0, 0.999)) << '\n';
}

hittable_list cornell_box() {
	hittable_list objects;

	auto red = make_shared<Lambertian>(vec3(.65, .05, .05));
	auto white = make_shared<Lambertian>(vec3(.73, .73, .73));
	auto green = make_shared<Lambertian>(vec3(.12, .45, .15));
	auto light = make_shared<DiffuseLight>(vec3(15, 15, 15));

	objects.add(make_shared<yz_rect>(0, 555, 0, 555, 555, green));
	objects.add(make_shared<yz_rect>(0, 555, 0, 555, 0, red));
	objects.add(make_shared<xz_rect>(213, 343, 227, 332, 554, light));
	objects.add(make_shared<xz_rect>(0, 555, 0, 555, 0, white));
	objects.add(make_shared<xz_rect>(0, 555, 0, 555, 555, white));
	objects.add(make_shared<xy_rect>(0, 555, 0, 555, 555, white));

	return objects;
}

void PT::render() {
	freopen("./out.ppm", "w", stdout);

	// image plane  
	double aspect_ratio = 16.0 / 9.0;
	//Camera camera(vec3(-2,2,1),vec3(0,0,-1),vec3(0,1,0),90.0,aspect_ratio);
	Camera camera(vec3(1.5f,0.0f,0.0f),vec3(0,0,0.0f),vec3(0,1,0),90.0,aspect_ratio);

	int h = 450;
	int w= int(camera.aspect_ratio * h);

	// world 
	auto R = cos(PI / 4);
	hittable_list world;

	if (0) {
		auto material_ground = std::make_shared<Lambertian>(vec3(0.8, 0.8, 0.0));
		auto material_center = std::make_shared<Lambertian>(vec3(0.1, 0.2, 0.5));
		auto material_left = std::make_shared<Dielectric>(1.5);
		auto material_right = std::make_shared<Metal>(vec3(0.8, 0.6, 0.2), 0.0);

		Sphere s1(vec3(0.0, -100.5, -1.0), 100.0, material_ground);
		Sphere s2(vec3(0.0, 0.0, -1.0), 0.5, material_center);
		Sphere s5(vec3(-1.0, 0.0, -1.0), 0.5, material_left);
		Sphere s3(vec3(-1.0, 0.0, -1.0), -0.45, material_left);
		Sphere s4(vec3(1.0, 0.0, -1.0), 0.5, material_right);
		world.add(std::make_shared<Sphere>(s1));
		world.add(std::make_shared<Sphere>(s2));
		world.add(std::make_shared<Sphere>(s3));
		world.add(std::make_shared<Sphere>(s4));
		world.add(std::make_shared<Sphere>(s5));
	}
	if (0)
	{
		auto earth_tex = std::make_shared<ImageTexture>("./asset/tex/earthmap.jpg");
		auto earth_material = std::make_shared<Lambertian>(earth_tex);
		auto light_material = std::make_shared<DiffuseLight>(vec3(2.0f,2.0f,2.0f));

		std::shared_ptr<Sphere> earth = std::make_shared<Sphere>(
			vec3(0.0, 0.0, -1.0), 0.5, earth_material);
		//std::shared_ptr<Sphere> light = std::make_shared<Sphere>(
			//vec3(1.0, 0.0, 0.0), 0.5, light_material);
		std::shared_ptr<xz_rect> light = std::make_shared<xz_rect>(
			-0.5f,0.5f,-0.5f,0.5f,1.0f,light_material);
		world.add(earth);
		world.add(light);
	}

	world = cornell_box();

	int samples_per_pixel = 50;
	int max_depth =10;

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

