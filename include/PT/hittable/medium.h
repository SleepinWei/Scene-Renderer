#pragma once

#include "../hittable.h"
#include <memory>
using std::shared_ptr;

class Ray;
class AABB;
class PTTexture;

class Medium : public hittable
{
public:
	Medium(shared_ptr<hittable> bound_, float d, shared_ptr<PTTexture> a);
	Medium(shared_ptr<hittable> bound_, float d, vec3 color);

	~Medium();
	virtual bool hit(
		const Ray &r, double t_min, double t_max, hit_record &rec) const override;

	virtual bool bounding_box(double time0, double time1, AABB &output_box) const override;

public:
	shared_ptr<hittable> boundary;
	shared_ptr<PTMaterial> phase_function;
	float neg_inv_density;
};
