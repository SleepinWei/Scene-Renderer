#pragma once
#include<glm/glm.hpp>
using glm::vec3;
#define PI 3.1415926f

bool nearZero(const vec3& v);
vec3 random_in_unit_sphere();
vec3 random_in_hemisphere(const vec3& normal);
float length_squared(const vec3& v);
vec3 refract(const vec3& uv, const vec3& n, float etai_over_etat);

