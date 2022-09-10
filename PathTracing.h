#pragma once
#include<vector>
#include<limits>
#include<cstdlib>
#include"PTVec.h"
#include"PTRay.h"

namespace PT {
	const double infinity = std::numeric_limits<double>::infinity();
	class hitRecord {
	public:
		vec3 p;
		vec3 normal;
		double t;
		bool front_face; 
		vec3 T;
		vec3 B;

		inline void set_face_normal(Ray& r, vec3& outward_normal);
	};

	class hittable {
	public:
		virtual bool hit(Ray& r, double t_min, double t_max, hitRecord& rec)=0;
	};

	class hittable_list : public hittable {
	public:
		hittable_list() = default;
		hittable_list(hittable* object); 

		void clear(); 
		void add(hittable* object);
		virtual bool hit(Ray& r, double t_min, double t_max, hitRecord& rec) override;

	public:
		std::vector<hittable*> objects;
	};

	class Sphere : public hittable{
	public:
		Sphere(vec3 center, double radius);
		~Sphere();

		virtual bool hit(Ray& r, double t_min, double t_max, hitRecord& rec) override;

	public:
		vec3 center;
		double radius;
	};

	vec3 rayColor(Ray r,hittable& world,int depth);

	void render();
}
