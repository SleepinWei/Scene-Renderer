#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
using glm::vec3;
using std::shared_ptr;

class PTScene; 
namespace PT
{
	class hittable_list;
	class hittable;
	class Camera;
	class Ray;
	class hit_record;
	class BVH_Node;

	class PTConfig{
	private:
		PTConfig(){};
		~PTConfig(){}; 

	public:
		static PTConfig *GetInstance() { 
			static PTConfig config;
			return &config;
		}

		bool bUseBVH : true;
	};

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
		void render(shared_ptr<PTScene> scene, int threadNum);
		void threadRender(shared_ptr<PTScene> scene,int start, int end,int id);
		void writeToFile(shared_ptr<PTScene> scene,const std::string &filename);

	public:
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
		vec3 rayColor(shared_ptr<PTScene> scene,const Ray &r, int depth);
		void write_color(const vec3 &color, int samples_per_pixel, int pos);
	};
}
