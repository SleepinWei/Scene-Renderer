#pragma once

#include"PTrandom.h"
#include"PTRay.h"
#include"PTVec.h"

namespace PT {
	class AABB {
	public:
		AABB() = default;
		AABB(const vec3& a, const vec3& b);

		bool hit(const Ray& r, double t_min, double t_max)const;

	public:
		vec3 minimum;
		vec3 maximum;
	};
	AABB surrounding_box(const AABB& box0, const AABB& box1);
}
