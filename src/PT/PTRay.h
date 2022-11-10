#pragma once
//#include"PTVec.h"
#include<glm/glm.hpp>
using glm::vec3;

namespace PT{
	class Ray{
	public:
		Ray() = default;
		~Ray();
		Ray(vec3 origin, vec3 direction);

		vec3 at(float t)const ;

	public:
		vec3 orig;
		vec3 dir;
	};

}
