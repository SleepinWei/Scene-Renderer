#pragma once
#include<thread>
#include<memory>
#include<vector>
#include<mutex>
#include<glm/glm.hpp>
using glm::vec3;

namespace PT {
	class hittable_list;
	class hittable;
	class Camera;
	class Ray;
	class hitRecord;
	
	class Renderer {
	public:
		Renderer(int samples,int max_depth);
		~Renderer();

		void render(int threadNum);
		void addCam(std::shared_ptr<Camera> cam);
		void addObject(std::shared_ptr<hittable> object);
		void threadRender(int start, int end);
		void writeToFile(const std::string& filename);
		
	public:
		std::shared_ptr<hittable_list> world;
		std::shared_ptr<Camera> camera;
		std::vector<std::thread> threads;
		int samples;
		int max_depth;
		int* resultImage; 
		int pixelCnt; 
		
		// mutex
		std::mutex printMtx;
		std::mutex writeMtx;
		//std::mutex 
	private:
		vec3 rayColor(const Ray& r, int depth);
		void write_color(const vec3& color, int samples_per_pixel,int pos);

	};
}
