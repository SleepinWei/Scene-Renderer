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
		vec3 operator* (double t)const;
		vec3 operator+ (double t)const;
		vec3 operator- (double t)const;
		vec3 operator/ (double t)const;
		vec3 operator+ (const vec3& v)const;
		vec3 operator- (const vec3& v)const;
		vec3 operator- (void)const;
		vec3 operator* (const vec3& v)const;
		vec3 operator/ (const vec3& v)const;

		inline static vec3 random();
		inline static vec3 random(double min, double max);
	};
	vec3 operator* (double t, const PT::vec3& v);
	vec3 operator+ (double t, const PT::vec3& v);
	vec3 operator- (double t, const PT::vec3& v);

	double dot(const vec3& a, const vec3& b);
	vec3 cross(const vec3& a, const vec3& b);
	vec3 normalize(const vec3& v);
	double length(const vec3& v);
	double length_squared(const vec3& v);

	vec3 reflect(const vec3& v, const vec3& n);
	vec3 refract(const vec3& uv, const vec3& n, double etai_over_etat);

	vec3 random_direction(); 
	vec3 random_in_hemisphere(const PT::vec3& normal);
	vec3 random_in_unit_sphere();
	bool nearZero(const vec3& v);

	double radians(double degree);
}
