#include"PTRay.h"
PT::Ray::Ray(vec3 origin, vec3 dir) :
	orig(origin), dir(dir) {
}

PT::Ray::~Ray() {
}

PT::vec3 PT::Ray::at(double t)const {
	return orig + t * dir;
}
