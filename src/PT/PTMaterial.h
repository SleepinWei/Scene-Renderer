#pragma once
#include<memory>
#include"PTRay.h"
#include"hittable.h"

using std::shared_ptr;

namespace PT {
	
	class Texture;

	class Material {
	public:
		virtual bool scatter(
			const Ray& r_in, const hitRecord& rec, vec3& attenuation, Ray& scattered) const= 0;
	};

	class Lambertian :public Material {
	public:
		Lambertian(const vec3& a);
		Lambertian(shared_ptr<Texture> a) :albedo(a) {};
		virtual bool scatter(
			const Ray& r, const hitRecord& rec,
			vec3& attenuation,
			Ray& scattered
		)const override;
		
	public:
		//vec3 albedo;
		std::shared_ptr<Texture> albedo;
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
		float fuzz;
	};

	class Dielectric : public Material {
	public:
		Dielectric(double index_of_refraction);

		virtual bool scatter(
			const Ray& r, const hitRecord& rec, vec3& attenuation, Ray& scattered
		)const override;
	public:
		float ir; 
	private:
		static double reflectance(double cosine, double ref_idx);
	};
}
