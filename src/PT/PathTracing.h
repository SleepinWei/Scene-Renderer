#pragma once
#include<vector>
#include<limits>
#include<cstdlib>
#include<glm/glm.hpp>
//#include"PTVec.h"
#include"PTRay.h"
#include"hittable.h"

using glm::vec3;

namespace PT {
	const double infinity = std::numeric_limits<double>::infinity();
	//const vec3 background = vec3(0.5, 0.7, 1.0);
	const vec3 background = vec3(0.0, 0.0, 0.0);

	vec3 rayColor(const Ray& r,const hittable& world,int depth);

	void render();
}

