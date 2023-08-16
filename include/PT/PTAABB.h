#pragma once

#include "PTrandom.h"
// #include"PTVec.h"
#include <glm/glm.hpp>
using glm::vec3;

namespace PT
{
	class Ray;

	class AABB
	{
	public:
		AABB() = default;
		AABB(const vec3 &a, const vec3 &b);
		AABB(const AABB &box_);

		bool hit(const Ray &r, double t_min, double t_max) const;

		static AABB surrounding_box(const AABB &box0, const AABB &box1);

		AABB pad()
		{
			// Return an AABB that has no side narrower than some delta, padding if necessary.
			double delta = 0.0001;
			vec3 new_min = minimum;
			vec3 new_max = maximum;
			if (maximum.x - minimum.x < delta)
			{
				new_max.x += delta;
				new_min.x -= delta;
			}
			if (maximum.y - minimum.y < delta)
			{
				new_max.y += delta;
				new_min.y -= delta;
			}
			if (maximum.z - minimum.z < delta)
			{
				new_max.z += delta;
				new_min.z -= delta;
			}
			return AABB(new_min, new_max);
		}

		vec3 max();
		vec3 min();

	public:
		vec3 minimum;
		vec3 maximum;
	};
}
