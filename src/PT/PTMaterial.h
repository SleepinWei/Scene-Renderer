#pragma once
#include"PTRay.h"
#include"hittable.h"

namespace PT {
	
	class Material {
	public:
		virtual bool scatter(
			const Ray& r_in, const hitRecord& rec, vec3& attenuation, Ray& scattered) const= 0;
	};

	class Lambertian :public Material {
	public:
		Lambertian(const vec3& a);
		virtual bool scatter(
			const Ray& r, const hitRecord& rec,
			vec3& attenuation,
			Ray& scattered
		)const override;
		
	public:
		vec3 albedo;
	};

	class Metal : public Material {
	public:
		Metal(const vec3& a,double f);
		virtual bool scatter(
			const Ray& r, const hitRecord& rec,
			vec3& attenuation,
			Ray& scattered
		) const override;
	public:
		vec3 albedo;
		double fuzz;
	};
	class Dielectric : public Material {
	public:
		Dielectric(double index_of_refraction);

		virtual bool scatter(
			const Ray& r, const hitRecord& rec, vec3& attenuation, Ray& scattered
		)const override;
	public:
		double ir; 
	private:
		static double reflectance(double cosine, double ref_idx);
	};
}
