#pragma once
#include"PTrandom.h"
namespace PT {
	class vec3 {
	public:
		vec3() = default;
		//vec3(glm::vec3& v);
		vec3(double x, double y, double z);
		vec3(double x);
		~vec3() = default;

		double x, y, z;
		vec3 operator* (double t);
		vec3 operator+ (double t);
		vec3 operator- (double t);
		vec3 operator/ (double t);
		vec3 operator+ (vec3 v);
		vec3 operator- (vec3 v);
		vec3 operator-(void);
		vec3 operator* (vec3& v);
		vec3 operator/ (vec3& v);

		inline static vec3 random();
		inline static vec3 random(double min, double max);
	};
	vec3 operator* (double t, PT::vec3 v);
	vec3 operator+ (double t, PT::vec3 v);
	vec3 operator- (double t, PT::vec3 v);

	double dot(vec3 a, vec3 b);
	vec3 cross(vec3 a, vec3 b);
	vec3 normalize(vec3 v);
	double length(vec3 v);

	vec3 reflect(vec3& v, vec3& n);

	vec3 random_direction(); 
}
