#pragma once
#include"PTRay.h"
#include"hittable.h"

namespace PT {
	vec3 random_in_hemisphere(PT::vec3& normal);
	
	class Material {
	public:
		virtual bool scatter(
			Ray& r_in, hitRecord& rec, vec3& attenuation, Ray& scattered) = 0;
	};

	class Lambertian :public Material {
	public:
		Lambertian(const vec3& a);
		virtual bool scatter(
			Ray& r, hitRecord& rec,
			vec3& attenuation,
			Ray& scattered
		);
		
	public:
		vec3 albedo;
	};

	class Metal : public Material {
	public:
		Metal(const vec3& a);
		virtual bool scatter(
			Ray& r, hitRecord& rec,
			vec3& attenuation,
			Ray& scattered
		) override;
	public:
		vec3 albedo;
	};
}
