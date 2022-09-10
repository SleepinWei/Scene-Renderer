#pragma once
#include"PTVec.h"
namespace PT{
	class Ray{
	public:
		Ray() = default;
		~Ray();
		Ray(vec3 origin, vec3 direction);

		vec3 at(double t);

	public:
		vec3 orig;
		vec3 dir;
	};

}
