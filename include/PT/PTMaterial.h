#pragma once
#include <glm/glm.hpp>
#include <memory>
using glm::vec3;
using std::shared_ptr;

namespace PT
{

	class Texture;
	class SolidColor;
	class Ray;
	class hittable;
	class hit_record;

	class Material
	{
	public:
		virtual bool scatter(
			const Ray &r_in, const hit_record &rec, vec3 &attenuation, Ray &scattered) const = 0;
		virtual vec3 emitted(float u, float v, const vec3 &p) const { return vec3(0.0f, 0.0f, 0.0f); }
		virtual double scattering_pdf(const Ray &r_in, const hit_record &rec, const Ray &scattered)
			const
		{
			return 0;
		}
	};

	class Lambertian : public Material
	{
	public:
		Lambertian(const vec3 &a);
		Lambertian(shared_ptr<Texture> a) : albedo(a){};

		virtual bool scatter(
			const Ray &r, const hit_record &rec,
			vec3 &attenuation,
			Ray &scattered) const override;
		virtual double scattering_pdf(const Ray &r_in, const hit_record &rec, const Ray &scattered) const override;
	public:
		// vec3 albedo;
		std::shared_ptr<Texture> albedo;
	};

	class Metal : public Material
	{
	public:
		Metal(const vec3 &a, double f);
		virtual bool scatter(
			const Ray &r, const hit_record &rec,
			vec3 &attenuation,
			Ray &scattered) const override;

	public:
		vec3 albedo;
		float fuzz;
	};

	class Dielectric : public Material
	{
	public:
		Dielectric(double index_of_refraction);

		virtual bool scatter(
			const Ray &r, const hit_record &rec, vec3 &attenuation, Ray &scattered) const override;

	public:
		float ir;

	private:
		static double reflectance(double cosine, double ref_idx);
	};

	class DiffuseLight : public Material
	{
	public:
		DiffuseLight(std::shared_ptr<Texture> a);
		DiffuseLight(vec3 c);

		virtual bool scatter(const Ray &r_in, const hit_record &rec, vec3 &attenuation, Ray &scattered) const override;

		virtual vec3 emitted(float u, float v, const vec3 &p) const override;

	public:
		std::shared_ptr<Texture> emit;
		float intensity;
	};
}
