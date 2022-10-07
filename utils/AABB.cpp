#include"AABB.h"

AABB::AABB() {
	this->min = glm::vec3(0.0f);
	this->max = glm::vec3(0.0f);
}

AABB::AABB(const glm::vec3& mi, const glm::vec3& ma) {
	min = mi; 
	max = ma; 
}

AABB::~AABB() {
	; 
}

