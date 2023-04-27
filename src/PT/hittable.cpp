#include"PT/hittable.h"
#include"PT/PTAABB.h"
#include"PT/PTRay.h"
#include"PT/PTMaterial.h"
using namespace PT;

void hittable::addTexture(std::shared_ptr<Material>& mat) {

}


PT::Sphere::Sphere(const vec3& center, double radius, std::shared_ptr<Material> material) :
	center(center),radius(radius),mat_ptr(material) {
	
}

void PT::hitRecord::set_face_normal(const Ray& r, const vec3& outward_normal) {
	this->front_face = dot(r.dir, outward_normal) < 0;
	this->normal = front_face ? outward_normal : -outward_normal;
}
PT::hittable_list::hittable_list(std::shared_ptr<hittable> object) {
	add(object);
}

void PT::hittable_list::clear() {
	objects.clear();
}

void PT::hittable_list::add(std::shared_ptr<hittable> object) {
	objects.push_back(object);
}

bool PT::hittable_list::hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const {
	hitRecord temp_rec; 
	bool hit_anything = false;
	auto closest = t_max;
	for (auto object : objects) {
		if (object->hit(r, t_min, closest, temp_rec)) {
			// if the ray hits anything and t is smaller than 
			// the current closest. 
			hit_anything = true;
			closest = temp_rec.t;
			rec = temp_rec;
		}
	}
	return hit_anything;
}

bool PT::Sphere::bounding_box(double time0, double time1, AABB& output_box)const {
	output_box = AABB(
		center - vec3(radius, radius, radius),
		center + vec3(radius, radius, radius)
	);
	return true;
}

bool PT::hittable_list::bounding_box(double time0, double time1, AABB& output_box)const{
	if (objects.empty()) return false;

	AABB temp_box;
	bool first_box = true;

	for (const auto& object : objects) {
		if (!object->bounding_box(time0, time1, temp_box)) return false;
		output_box = first_box ? temp_box : AABB::surrounding_box(output_box, temp_box);
		first_box = false;
	}

	return true;
}

void PT::Sphere::get_sphere_uv(const vec3& p, float& u, float& v) {
	// p: a given point on the sphere of radius one, centered at the origin.
	// u: returned value [0,1] of angle around the Y axis from X=-1.
	// v: returned value [0,1] of angle from Y=-1 to Y=+1.
	//     <1 0 0> yields <0.50 0.50>       <-1  0  0> yields <0.00 0.50>
	//     <0 1 0> yields <0.50 1.00>       < 0 -1  0> yields <0.50 0.00>
	//     <0 0 1> yields <0.25 0.50>       < 0  0 -1> yields <0.75 0.50>

	auto theta = acos(-p.y);
	auto phi = atan2(-p.z, p.x) + PI;

	u = phi / (2 * PI);
	v = theta / PI;
}

PT::Sphere::~Sphere() {

}

bool PT::Sphere::hit(const Ray& r,double t_min,double t_max,hitRecord& rec)const {
	float t = -1.0f;
	vec3 ooc = r.orig - center; 
	float a = dot(r.dir, r.dir);
	float half_b = dot(ooc, r.dir);
	float c = dot(ooc, ooc) - radius * radius;

	float delta = half_b * half_b - a * c;
	if (delta < 0) {
		return false; 
	}
	float sqrtdelta = sqrtf(delta);
	float root = (-half_b - sqrtdelta) / a;
	if (root < t_min || t_max < root) {
		root = (- half_b + sqrtdelta) / a;
		if (root < t_min || t_max < root) {
			return false;
		}
	}
	rec.t = root;
	rec.p = r.at(rec.t);
	vec3 outward_normal = (rec.p - center) / radius; 
	rec.set_face_normal(r, outward_normal);
	this->get_sphere_uv(outward_normal, rec.u, rec.v);
	rec.mat_ptr = mat_ptr;
	//rec.T = normalize(vec3(-rec.p.y, rec.p.x, 0));
	//rec.B = normalize(cross(rec.normal, rec.T));

	return true;
}

void PT::Sphere::addTexture(std::shared_ptr<Material>& mat) {
	this->mat_ptr = mat;
}


