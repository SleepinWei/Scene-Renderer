#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
using glm::vec3;

namespace PT
{
	class hittable_list;
	class hittable;
	class Camera;
	class Ray;
	class hit_record;
	class BVH_Node;

	class Renderer
	{
	private:
		Renderer();
		~Renderer();

	public:
		static Renderer *GetInstance()
		{
			static Renderer renderer;
			return &renderer;
		}
		void init(int samples, int max_depth);
		void render(int threadNum);
		void addCam(std::shared_ptr<Camera> cam);
		void addObject(std::shared_ptr<hittable> object);
		void buildBVH();
		void addLight(std::shared_ptr<hittable> object);
		void threadRender(int start, int end);
		void writeToFile(const std::string &filename);

	public:
		std::shared_ptr<hittable_list> _world;
		std::shared_ptr<BVH_Node> world; 
		std::shared_ptr<hittable_list> lights; 
		std::shared_ptr<Camera> camera;
		std::vector<std::thread> threads;
		int samples;
		int max_depth;
		int *resultImage;
		int pixelCnt;

		// mutex
		std::mutex printMtx;
		std::mutex writeMtx;
		// std::mutex
	private:
		vec3 rayColor(const Ray &r, int depth);
		void write_color(const vec3 &color, int samples_per_pixel, int pos);
	};
}
