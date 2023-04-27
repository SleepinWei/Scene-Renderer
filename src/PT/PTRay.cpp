#include"PT/PTRay.h"
PT::Ray::Ray(vec3 origin, vec3 dir) :
	orig(origin), dir(dir) {
}

PT::Ray::~Ray() {
}

vec3 PT::Ray::at(float t)const {
	return orig + t * dir;
}
