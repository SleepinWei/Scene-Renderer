#include "PT\PTRenderer.h"
#include "PT/PTRenderer.h"
#include "PT\PTCamera.h"
#include "PT\PTMaterial.h"
#include "PT\PTRay.h"
#include "PT\PTScene.h"
#include "PT\PTrandom.h"
#include "PT\hittable.h"
#include "utils/log.h"
#include <PT/BVH.h>
#include <PT/pdf.h>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <utility>


using std::make_shared;

void PTRenderer::write_color(const vec3 &color, int samples_per_pixel, int pos)
{
	auto r = color.x;
	auto g = color.y;
	auto b = color.z;
	// Replace NaN components with zero.
	if (r != r)
		r = 0.0;
	if (g != g)
		g = 0.0;
	if (b != b)
		b = 0.0;
	float scale = 1.0f / samples_per_pixel;

	r = sqrtf(scale * r);
	g = sqrtf(scale * g);
	b = sqrtf(scale * b);
	// r = scale * r;
	// g = scale * g;
	// b = scale * b;

	int index = (pos)*3;

	writeMtx.lock();
	resultImage[index] = int(255 * clamp(r, 0.0, 1.0));
	resultImage[index + 1] = int(255 * clamp(g, 0.0, 1.0));
	resultImage[index + 2] = int(255 * clamp(b, 0.0, 1.0));
	++pixelCnt;
	writeMtx.unlock();
}

PTRenderer::PTRenderer()
{
	this->samples = 0;
	this->max_depth = 0;
	this->pixelCnt = 0;
	background = vec3(0, 0, 0);
}

void PTRenderer::init(int samples, int max_depth)
{
	this->samples = samples;
	this->max_depth = max_depth;
	this->pixelCnt = 0;
}

PTRenderer::~PTRenderer()
{
	delete[] resultImage;
}

void PTRenderer::render(shared_ptr<PTScene> scene, int threadNum)
{
	auto camera = scene->camera;

	if (!camera)
	{
		LOG_ERROR("PTScene no camera");
		return;
	}
	int w = camera->width;
	int h = camera->height;

	// allocate space
	resultImage = new int[w * h * 3];

	int interval = h / threadNum;
	if (interval * threadNum != h)
	{
		interval += 1;
	}

	int thread_num = std::min((unsigned int)h, std::max(1u,std::thread::hardware_concurrency() - 1));
	LOG_INFO("Running with " << thread_num << "threads");
	for (int i = 0; i < thread_num; i++)
	{
		int start = i * interval;
		int end = std::min(start + interval - 1, h - 1);
		std::thread t(&PTRenderer::threadRender, this, scene, start, end, i);
		threads.push_back(std::move(t));
	}
}

vec3 PTRenderer::rayColor(shared_ptr<PTScene> scene, const Ray &r, int depth)
{
	auto world = scene->getWorld();
	auto lights = scene->lights;

	hit_record rec;

	if (depth < 0)
	{
		return vec3(0, 0, 0);
	}

	bool isHit = world->hit(r, 0.001, PT_INFINITY, rec);
	if (!isHit)
	{
		// hit other objects 
		if(!scene->other_objects->hit(r,0.001,PT_INFINITY,rec)){
			return background;
		}
	}

	// scatter direction 
	scatter_record srec;
	vec3 emitted = rec.mat_ptr->emitted(rec.u, rec.v, rec.p);

	bool hasScatter = rec.mat_ptr->scatter(r, rec, srec);
	if (!hasScatter)
	{
		return emitted;
	}

	if (srec.is_specular)
	{
		return srec.attenuation * rayColor(scene, srec.specular_ray, depth - 1);
	}

	shared_ptr<pdf> light_ptr = make_shared<hittable_pdf>(lights, rec.p);
	if(lights->objects.size() == 0){
		// no ordinary lights
		light_ptr = srec.pdf_ptr;
	}
	mixture_pdf p(light_ptr, srec.pdf_ptr);

	Ray scattered = Ray(rec.p, p.generate());
	double pdf_val = p.value(scattered.dir);

	float scattering_pdf = rec.mat_ptr->scattering_pdf(r, rec, scattered);

	vec3 color_from_scatter =
		(srec.attenuation * scattering_pdf * rayColor(scene, scattered, depth - 1)) / (float)pdf_val;
	// vec3 color_from_scatter = attenuation * rayColor(scattered, depth - 1);

	vec3 finalColor = emitted + color_from_scatter;
	return finalColor;
}

void PTRenderer::threadRender(shared_ptr<PTScene> scene, int start, int end, int id)
{
	auto camera = scene->camera;

	int h = camera->height;
	int w = camera->width;

	int samples_per_pixel = this->samples;

	for (int j = end; j >= start; j--)
	{
		if (id == 0 && (end - j) % 3 == 0)
		{
			printMtx.lock();
			std::cerr << "Thread: " << id << " Progress: " << (end - j) * 1.0 / (end - start) * 100 << " % \n";
			printMtx.unlock();
		}

		for (int i = 0; i < w; i++)
		{
			vec3 color(0, 0, 0);
			for (int s = 0; s < samples_per_pixel; s++)
			{
				auto u = (i + random_double()) / (w - 1);
				auto v = (j + random_double()) / (h - 1);
				Ray r = camera->get_ray(u, v);
				color = color + rayColor(scene, r, max_depth);
			}

			int pos = j * w + i;
			write_color(color, samples_per_pixel, pos);
		}
	}
}
void PTRenderer::writeToFile(shared_ptr<PTScene> scene, const std::string &filename)
{
	for (int i = 0; i < threads.size(); i++)
	{
		threads[i].join();
	}
	threads.clear();
	std::vector<std::thread>().swap(threads); // 释放

	LOG_INFO("TRACING DONE!");

	auto camera = scene->camera;

	int w = camera->width;
	int h = camera->height;
	freopen(filename.c_str(), "w", stdout);
	std::cout << "P3\n"
			  << w << ' ' << h << "\n255\n";

	for (int j = h - 1; j >= 0; j--)
	{
		for (int i = 0; i < w; i++)
		{
			int index = j * w + i;
			std::cout << resultImage[3 * index] << ' ' << resultImage[3 * index + 1]
					  << ' ' << resultImage[3 * index + 2] << '\n';
		}
	}
	fclose(stdout);
}

void PTConfig::parse(json &data)
{
	this->samples = data["samples"].get<int>();
	this->max_depth = data["max_depth"].get<int>();
	this->bUseBVH = data["useBVH"].get<bool>();
}
