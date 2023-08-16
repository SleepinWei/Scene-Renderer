#pragma once
#include <glm/glm.hpp>
#include <memory>
#include<PT/PTRay.h>

using glm::vec3;
using std::shared_ptr;

class pdf;
namespace PT
{

	class Texture;
	class SolidColor;
	class Ray;
	class hittable;
	class hit_record;

	struct scatter_record
	{
		Ray specular_ray;
		bool is_specular;
		vec3 attenuation;
		shared_ptr<pdf> pdf_ptr;
	};

	class Material
	{
	public:
		virtual bool scatter(
			const Ray &r_in, const hit_record &rec, scatter_record &srec) const = 0;
		virtual vec3 emitted(float u, float v, const vec3 &p) const
		{
			return vec3(0.0f, 0.0f, 0.0f);
		}
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
			const Ray &r, const hit_record &rec, scatter_record &srec) const override;
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
			const Ray &r, const hit_record &rec, scatter_record &srec) const override;

	public:
		vec3 albedo;
		float fuzz;
	};

	class Dielectric : public Material
	{
	public:
		Dielectric(double index_of_refraction);

		virtual bool scatter(
			const Ray &r, const hit_record &rec, scatter_record &srec) const override;

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

		virtual bool scatter(const Ray &r_in, const hit_record &rec, scatter_record &srec)
			const override;

		virtual vec3 emitted(float u, float v, const vec3 &p) const override;

	public:
		std::shared_ptr<Texture> emit;
		float intensity;
	};

	class Isotropic : public Material
	{
	public:
		Isotropic(vec3 c);
		Isotropic(shared_ptr<Texture> a);

		virtual bool scatter(
			const Ray &r_in, const hit_record &rec, scatter_record &srec) const override;

		virtual double scattering_pdf(const Ray &r_in, const hit_record &rec, const Ray &scattered)
			const override;

	public:
		shared_ptr<Texture> albedo;
	};
}
