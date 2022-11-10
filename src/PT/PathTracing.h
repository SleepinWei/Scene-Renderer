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
	
	vec3 rayColor(const Ray& r,const hittable& world,int depth);

	void render();
}

