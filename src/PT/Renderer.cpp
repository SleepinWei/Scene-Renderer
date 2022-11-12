#include<glm/glm.hpp>
#include"Renderer.h"
#include<utility>
#include<iostream>
#include"hittable.h"
#include"PTCamera.h"
#include"PTRay.h"
#include"PTrandom.h"
#include"PTMaterial.h"

using namespace PT;

void Renderer::write_color(const vec3& color, int samples_per_pixel) {
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

Renderer::Renderer(int samples,int max_depth) {
	world = std::make_shared<hittable_list>();
	camera = nullptr;
	this->samples = samples;
	this->max_depth = max_depth;
}

Renderer::~Renderer() {
	for (int i = 0; i < threads.size(); i++) {
		threads[i].join();
	}
}

void Renderer::addCam(std::shared_ptr<Camera> cam) {
	camera = cam;
}

void Renderer::addObject(std::shared_ptr<hittable> object) {
	world->add(object);
}

void Renderer::render(int threadNum) {
	int w = camera->width;
	int h = camera->height;
	int interval = h / threadNum;
	if (interval * threadNum != h) {
		interval += 1;
	}

	std::cout << "P3\n" << w << ' ' << h << "\n255\n";
	for (int i = 0; i < threadNum; i++) {
		int start = i * interval;
		int end = std::min(start + interval, h - 1);
		std::thread t(&Renderer::threadRender,this,start, end);
		threads.push_back(std::move(t));
	}
}

vec3 Renderer::rayColor(const Ray& r,int depth) {
	hitRecord rec;

	if (depth <= 0) {
		return vec3(0, 0, 0);
	}

	bool isHit = world->hit(r,0.001,infinity,rec);
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

	vec3 color = rayColor(scattered, depth - 1);
	vec3 finalColor = emitted + attenuation * color;
	return finalColor;
}

void Renderer::threadRender(int start, int end) {
	int h = camera->height;
	int w = camera->width;

	int samples_per_pixel = this->samples;
	auto id = std::this_thread::get_id();

	for (int j = end; j >=start; j--) {
		printMtx.lock();
		std::cerr << "Thread: " << id << " Progress: " << (end-j) *1.0/ (end-start) *100 << " % \n";
		printMtx.unlock();

		for (int i = 0; i < w; i++) {
			vec3 color(0, 0, 0);
			for (int s = 0; s < samples_per_pixel; s++) {
				auto u = (i + random_double()) / (w - 1);
				auto v = (j + random_double()) / (h - 1);
				Ray r = camera->get_ray(u, v);
				color = color + rayColor(r, max_depth);
			}
			
			writeMtx.lock();
			write_color(color, samples_per_pixel);
			writeMtx.unlock();
		}
	}

}
