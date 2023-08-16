#include "PT/PTMaterial.h"
#include "PT/PTRay.h"
#include "PT/PTTexture.h"
#include "PT/PTrandom.h"
#include "PT/glm_vec3.h"
#include "PT/hittable.h"
#include "PT/onb.h"
#include "PT/pdf.h"
#include <iostream>
using std::make_shared;

using namespace PT;
using glm::normalize;

PT::Lambertian::Lambertian(const vec3 &a)
	: albedo(std::make_shared<SolidColor>(a))
{
}
bool PT::Lambertian::scatter(
	const Ray &r, const hit_record &rec, scatter_record &srec) const
{
	srec.is_specular = false;
	srec.attenuation = albedo->value(rec.u, rec.v, rec.p);
	srec.pdf_ptr = make_shared<cosine_pdf>(rec.normal);
	return true;
}

double Lambertian::scattering_pdf(const Ray &r_in, const hit_record &rec, const Ray &scattered) const
{
	auto cos_theta = glm::dot(rec.normal, glm::normalize(scattered.dir));
	return cos_theta < 0 ? 0 : cos_theta / PI;
}

PT::Metal::Metal(const vec3 &a, double f) : albedo(a), fuzz(f < 1 ? f : 1)
{
}
bool PT::Metal::scatter(
	const Ray &r_in, const hit_record &rec, scatter_record &srec) const
{
	vec3 reflected = glm::reflect(glm::normalize(r_in.dir), rec.normal);
	srec.specular_ray =
		Ray(rec.p, reflected + fuzz * random_in_unit_sphere());
	srec.attenuation = albedo;
	srec.is_specular = true;
	srec.pdf_ptr = nullptr;
	return true;
}
PT::Dielectric::Dielectric(double index_of_refraction) : ir(index_of_refraction)
{
}

bool PT::Dielectric::scatter(
	const Ray &r_in, const hit_record &rec, scatter_record &srec) const
{
	srec.is_specular = true;
	srec.pdf_ptr = nullptr;
	srec.attenuation = vec3(1.0, 1.0, 1.0);
	double refraction_ratio = rec.front_face ? (1.0 / ir) : ir;

	vec3 unit_direction = glm::normalize(r_in.dir);
	double cos_theta = fmin(dot(-unit_direction, rec.normal), 1.0);
	double sin_theta = sqrt(1.0 - cos_theta * cos_theta);

	bool cannot_refract = refraction_ratio * sin_theta > 1.0;
	vec3 direction;

	if (cannot_refract || reflectance(cos_theta, refraction_ratio) > random_double())
		direction = reflect(unit_direction, rec.normal);
	else
		direction = refract(unit_direction, rec.normal, refraction_ratio);

	srec.specular_ray = Ray(rec.p, direction);
	return true;
}
double PT::Dielectric::reflectance(double cosine, double ref_idx)
{
	// Use Schlik's approximation
	auto r0 = (1 - ref_idx) / (1 + ref_idx);
	r0 = r0 * r0;
	return r0 + (1 - r0) * pow((1 - cosine), 5);
}

bool DiffuseLight::scatter(const Ray &r_in, const hit_record &rec, scatter_record &srec) const
{
	// lights have no scattering
	return false;
}

vec3 DiffuseLight::emitted(float u, float v, const vec3 &p) const
{
	//
	return emit->value(u, v, p);
}

DiffuseLight::DiffuseLight(vec3 c)
{
	emit = std::static_pointer_cast<Texture>(
		std::make_shared<SolidColor>(c));
	intensity = 1.0f;
}
DiffuseLight::DiffuseLight(std::shared_ptr<Texture> a) : emit(a)
{
	intensity = 1.0f;
}

Isotropic::Isotropic(vec3 c) : albedo(std::make_shared<SolidColor>(c)) {}
Isotropic::Isotropic(shared_ptr<Texture> a) : albedo(a) {}

// never called and should not be called; 
bool Isotropic::scatter(
	const Ray &r_in, const hit_record &rec, scatter_record &srec) const
{
	srec.is_specular = true;
	srec.pdf_ptr = nullptr;
	srec.attenuation = vec3(1.0, 1.0, 1.0);
	srec.specular_ray = Ray(rec.p, random_in_unit_sphere());
	return true;
}

double Isotropic::scattering_pdf(const Ray &r_in, const hit_record &rec, const Ray &scattered) const
{
	return 1.0f / (4 * PI);
}
