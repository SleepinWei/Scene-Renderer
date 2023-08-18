#include"PT/PTRay.h"
Ray::Ray(vec3 origin, vec3 dir) :
	orig(origin), dir(dir) {
}

Ray::~Ray() {
}

vec3 Ray::at(float t)const {
	return orig + t * dir;
}
