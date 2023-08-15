#include"isotropic.h"
#include"PT/PTRay.h"
#include"PT/hittable.h"
#include"PT/PTTexture.h"
#include"PT/glm_vec3.h"

using namespace PT;
using std::make_shared;
using std::shared_ptr;

Isotropic::Isotropic(vec3 c) : albedo(make_shared<SolidColor>(c)) {}
Isotropic::Isotropic(shared_ptr<Texture> a) : albedo(a) {}

bool Isotropic::scatter(
	const Ray& r_in, const hit_record& rec, vec3& attenuation, Ray& scattered
) const {
	scattered = Ray(rec.p, random_in_unit_sphere());//, r_in.time);
	attenuation = albedo->value(rec.u, rec.v, rec.p);
	return true;
}

