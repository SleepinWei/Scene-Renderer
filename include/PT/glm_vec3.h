#pragma once
#include<glm/glm.hpp>
#include<PT/PTrandom.h>
using glm::vec3;
#define PI 3.1415926f
constexpr double PT_INFINITY = std::numeric_limits<double>::max();

bool nearZero(const vec3& v);
//vec3 random_in_unit_sphere();
vec3 random_in_hemisphere(const vec3& normal);
float length_squared(const vec3& v);
vec3 refract(const vec3& uv, const vec3& n, float etai_over_etat);
vec3 random_in_unit_sphere();
vec3 random_unit_vector();
vec3 random_cosine_direction(); 

// uniformly sample a sphere
inline vec3 random_to_sphere(double radius, double distance_squared) {
    auto r1 = random_double();
    auto r2 = random_double();
    auto z = 1 + r2*(sqrt(1-radius*radius/distance_squared) - 1);

    auto phi = 2*PI*r1;
    auto x = cos(phi)*sqrt(1-z*z);
    auto y = sin(phi)*sqrt(1-z*z);

    return vec3(x, y, z);
}
