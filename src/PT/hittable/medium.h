#pragma once

#include<memory>
#include"PT/hittable.h"
using std::shared_ptr;

namespace PT {
	class Ray;
	class AABB;
	class Texture;

	class Medium :public hittable {
	public:
		Medium(shared_ptr<hittable> bound_, float d, shared_ptr<Texture> a);
		Medium(shared_ptr<hittable> bound_, float d, vec3 color);

		~Medium();
		virtual bool hit(
			const Ray& r, double t_min, double t_max, hitRecord& rec) const override;

		virtual bool bounding_box(double time0, double time1, AABB& output_box) const override;
	public:
		shared_ptr<hittable> boundary;
		shared_ptr<Material> phase_function;
		float neg_inv_density;
	};

}
