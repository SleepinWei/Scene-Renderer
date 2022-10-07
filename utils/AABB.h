#pragma once
#include<glm/glm.hpp>

class AABB {
public:
	AABB();
	AABB(const glm::vec3& mi, const glm::vec3& ma);
	~AABB();

public:
	glm::vec3 min; 
	glm::vec3 max; 
};
