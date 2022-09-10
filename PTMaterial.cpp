#include"PTMaterial.h"

PT::vec3 PT::random_in_hemisphere(PT::vec3& normal) {
		PT::vec3 random_dir = PT::random_direction();
		if (dot(random_dir, normal) > 0.0) {
			return random_dir;
		}

		return -random_dir;
	}

PT::Lambertian::Lambertian(const vec3& a):albedo(a){
}
bool PT::Lambertian::scatter(
	Ray& r,hitRecord & rec,
	vec3& attenuation, 
	Ray& scattered
) 
{
	auto scatter_direction = random_in_hemisphere(rec.normal);
	scattered = Ray(rec.p, scatter_direction);
	attenuation = albedo;
	return true;
}
PT::Metal::Metal(const vec3 & a):albedo(a){
}
bool PT::Metal::scatter(
	Ray& r, hitRecord& rec,
	vec3& attenuation,
	Ray& scattered
	) {
	vec3 dir = normalize(r.dir);
	vec3 reflected = reflect(dir, rec.normal);
	scattered = Ray(rec.p, reflected);
	attenuation = albedo;
	return (dot(scattered.dir, rec.normal) > 0);
}
