#pragma once
#include"PTVec.h"
#include"PTRay.h"
//#include"PTMaterial.h"

namespace PT {
	class Material; 

	class hitRecord {
	public:
		vec3 p;
		vec3 normal;
		double t;
		bool front_face;
		Material* mat_ptr; // pointer of material
		//vec3 T;
		//vec3 B;

		inline void set_face_normal(const Ray& r, const vec3& outward_normal);
	};
	class hittable {
	public:
		virtual bool hit(const Ray& r, double t_min, double t_max, hitRecord& rec)const =0;
	};

	class hittable_list : public hittable {
	public:
		hittable_list() = default;
		hittable_list(hittable* object); 

		void clear(); 
		void add(hittable* object);
		virtual bool hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const override;

	public:
		std::vector<hittable*> objects;
	};

	class Sphere : public hittable{
	public:
		Sphere(const vec3& center, double radius,Material* m);
		~Sphere();

		virtual bool hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const override;

	public:
		vec3 center;
		double radius;
		Material* mat_ptr;
	};

}
