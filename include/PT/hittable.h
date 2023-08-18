#pragma once
#include <PT/PTrandom.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

using glm::vec3;
using std::shared_ptr;

namespace PT
{
	class PTMaterial;
	class AABB;
	class Ray;

	class hit_record
	{
	public:
		hit_record()
		{
			mat_ptr = nullptr;
		}

	public:
		vec3 p;
		vec3 normal;
		float t;
		float u;
		float v;
		bool front_face;
		std::shared_ptr<PTMaterial> mat_ptr; // pointer of material
		// vec3 T;
		// vec3 B;

		inline void set_face_normal(const Ray &r, const vec3 &outward_normal);
	};

	class hittable
	{
	public:
		virtual bool hit(const Ray &r, double t_min, double t_max, hit_record &rec) const = 0;
		virtual bool bounding_box(double time0, double time1, AABB &output_box) const = 0;
		virtual void addTexture(std::shared_ptr<PTMaterial> &mat);
		// virtual void setModel(glm::mat4 model) {}

		// for sampling light
		virtual double pdf_value(const vec3 &o, const vec3 &v) const
		{
			return 0.0;
		}

		virtual vec3 random(const vec3 &o) const
		{
			return vec3(1, 0, 0);
		}
	};

	class hittable_list : public hittable
	{
	public:
		hittable_list() = default;
		hittable_list(std::shared_ptr<hittable> object);

		void clear();
		void add(std::shared_ptr<hittable> object);
		virtual bool hit(const Ray &r, double t_min, double t_max, hit_record &rec) const override;
		virtual bool bounding_box(double time0, double time1, AABB &output_box) const override;

		double pdf_value(const vec3 &o, const vec3 &v) const override
		{
			auto weight = 1.0 / objects.size();
			auto sum = 0.0;

			for (const auto &object : objects)
				sum += weight * object->pdf_value(o, v);

			return sum;
		}
		vec3 random(const vec3 &o) const override
		{
			auto int_size = static_cast<int>(objects.size());
			return objects[random_int(0, int_size - 1)]->random(o);
		}

	public:
		std::vector<std::shared_ptr<hittable>> objects;
	};

	class Sphere : public hittable
	{
	public:
		Sphere(const vec3 &center, double radius, std::shared_ptr<PTMaterial> m);
		~Sphere();

		virtual bool hit(const Ray &r, double t_min, double t_max, hit_record &rec) const override;
		virtual bool bounding_box(double time0, double time1, AABB &output_box) const;
		virtual void addTexture(std::shared_ptr<PTMaterial> &mat);

		static void get_sphere_uv(const vec3 &p, float &u, float &v);
		double pdf_value(const vec3 &o, const vec3 &v) const;
		vec3 random(const vec3 &o) const;

	public:
		vec3 center;
		float radius;
		std::shared_ptr<PTMaterial> mat_ptr;
	};

	struct PTVertex{
		vec3 pos; 
		vec3 normal; 
		glm::vec2 uv; 
	};

	class Triangle : public hittable
	{
	public:
		Triangle(const PTVertex &a, const PTVertex &b, const PTVertex &c,const shared_ptr<PTMaterial>& m);
		~Triangle();

		virtual bool hit(const Ray &r, double t_min, double t_max, hit_record &rec) const override;
		virtual bool bounding_box(double time0, double time1, AABB &output_box) const;

	public:
		PTVertex a, b, c;
		std::shared_ptr<PTMaterial> mat_ptr;
	};
}
