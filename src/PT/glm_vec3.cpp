#include"glm_vec3.h"
#include"PTrandom.h"

bool nearZero(const vec3& v) {
	return (fabs(v.x) < 1e-6) && (fabs(v.y) < 1e-6) && (fabs(v.z) < 1e-6);
}

//vec3 random_in_unit_sphere() {
	//double theta = random_double(0, PI);
	//double phi = random_double(0, 2 * PI);
	//return vec3(sin(theta) * sin(phi), cos(theta), sin(theta) * cos(phi));
//}

vec3 random_in_hemisphere(const vec3& normal) {
	vec3 random_dir = random_in_unit_sphere();
	if (dot(random_dir, normal) > 0.0) {
		return random_dir;
	}

	return -random_dir;
}

float length_squared(const vec3& v) {
	return v.x * v.x + v.y * v.y + v.z * v.z;
}

vec3 refract(const vec3& uv, const vec3& n, float etai_over_etat) {
	float cos_theta = fmin(dot(-uv, n), 1.0);
	vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
	vec3 r_out_parallel = -sqrtf(fabs(1.0 - length_squared(r_out_perp))) * n;
	return r_out_perp + r_out_parallel;
}
vec3 random_in_unit_sphere(){
    while (true) {
        auto p = vec3(random_double(-1, 1),random_double(-1,1),random_double(-1,1));
        if (glm::dot(p,p)>= 1) continue;
        return p;
    }
}

vec3 random_unit_vector() {
	return glm::normalize(random_in_unit_sphere());
}
