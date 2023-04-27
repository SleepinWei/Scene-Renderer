#pragma once

#include"PTrandom.h"
//#include"PTVec.h"
#include<glm/glm.hpp>
using glm::vec3;

namespace PT {
	class Ray;

	class AABB {
	public:
		AABB() = default;
		AABB(const vec3& a, const vec3& b);
		AABB(const AABB& box_);

		bool hit(const Ray& r, double t_min, double t_max)const;

		static AABB surrounding_box(const AABB& box0, const AABB& box1);

		vec3 max();
		vec3 min();
	public:
		vec3 minimum;
		vec3 maximum;
	};
}
