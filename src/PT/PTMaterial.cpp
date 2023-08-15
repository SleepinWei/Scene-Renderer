#include"PT/PTRay.h"
#include"PT/hittable.h"
#include"PT/PTMaterial.h"
#include"PT/PTTexture.h"
#include"PT/glm_vec3.h"
#include"PT/PTrandom.h"

using namespace PT;
using glm::normalize;

PT::Lambertian::Lambertian(const vec3& a)
	:albedo(std::make_shared<SolidColor>(a)){
}
#include<iostream>
bool PT::Lambertian::scatter(
	const Ray& r,const hit_record & rec,
	vec3& attenuation, 
	Ray& scattered
) const 
{
	//auto scatter_direction = random_in_hemisphere(rec.normal);
	auto scatter_direction = rec.normal + random_unit_vector();
	if (nearZero(scatter_direction)) {
		scatter_direction = rec.normal;
	}
	else {
		scatter_direction = normalize(scatter_direction);
	}
	scattered = Ray(rec.p, scatter_direction);
	attenuation = albedo->value(rec.u,rec.v,rec.p);
	return true;
}
PT::Metal::Metal(const vec3 & a,double f):albedo(a),fuzz(f<1?f:1) {
}
bool PT::Metal::scatter(
	const Ray& r, const hit_record& rec,
	vec3& attenuation,
	Ray& scattered
	) const {
	vec3 dir = normalize(r.dir);
	vec3 reflected = reflect(dir, rec.normal);
	scattered = Ray(rec.p, reflected + fuzz * random_in_unit_sphere());
	attenuation = albedo;
	return (dot(scattered.dir, rec.normal) > 0);
}
PT::Dielectric::Dielectric(double index_of_refraction) : ir(index_of_refraction) {
}

bool PT::Dielectric::scatter(
	const Ray& r_in, const hit_record& rec, vec3& attenuation, Ray& scattered
)const {
	attenuation = vec3(1.0, 1.0, 1.0);
	double refraction_ratio = rec.front_face ? (1.0 / ir) : ir;

	vec3 unit_direction = normalize(r_in.dir);
	//vec3 refracted = refract(unit_direction, rec.normal, refraction_ratio);
	double cos_theta = fmin(dot(-unit_direction, rec.normal), 1.0);
	double sin_theta = sqrt(1.0 - cos_theta * cos_theta);

	bool cannot_refract = refraction_ratio * sin_theta > 1.0;
	vec3 direction;

	if (cannot_refract||reflectance(cos_theta,refraction_ratio)>random_double())
	//	// 按照概率反射，概率为 reflectance
		direction = reflect(unit_direction, rec.normal);
	else
		direction = refract(unit_direction, rec.normal, refraction_ratio);

	scattered = Ray(rec.p, normalize(direction));
	return true;
}
double PT::Dielectric::reflectance(double cosine, double ref_idx) {
	// Use Schlik's approximation
	auto r0 = (1 - ref_idx) / (1 + ref_idx);
	r0 = r0 * r0;
	return r0 + (1 - r0) * pow((1 - cosine), 5);
}
	

bool DiffuseLight::scatter(const Ray& r_in, const hit_record& rec, vec3& attenuation, Ray& scattered)const {
	// lights have no scattering
	return false; 
}

vec3 DiffuseLight::emitted(float u, float v, const vec3& p)const {
	// 
	return emit->value(u, v, p);
}

DiffuseLight::DiffuseLight(vec3 c) {
	emit = std::static_pointer_cast<Texture>(
		std::make_shared<SolidColor>(c));
	intensity = 1.0f;
}
DiffuseLight::DiffuseLight(std::shared_ptr<Texture> a) :emit(a) 
{
	intensity = 1.0f;
}

double Lambertian::scattering_pdf(const Ray &r_in, const hit_record &rec, const Ray &scattered) const
{
	auto cos_theta = glm::dot(rec.normal, glm::normalize(scattered.dir));
	return cos_theta < 0 ? 0 : cos_theta / PI;
}
