#include"hittable.h"
PT::Sphere::Sphere(const vec3& center,double radius,Material* material):
	center(center),radius(radius),mat_ptr(material) {
	
}

PT::Sphere::~Sphere() {

}

bool PT::Sphere::hit(Ray& r,double t_min,double t_max,hitRecord& rec){
	double t = -1.0f;
	vec3 ooc = r.orig - center; 
	double a = dot(r.dir, r.dir);
	double b = 2.0f * dot(ooc, r.dir);
	double c = dot(ooc, ooc) - radius * radius;

	double delta = b * b - 4 * a * c;
	if (delta < 1e-6) {
		return false; 
	}
	double sqrtdelta = sqrt(delta);
	double root = (-b - sqrtdelta) / (2.0 * a);
	if (root < t_min || t_max < root) {
		root = (- b + sqrtdelta) / (2.0 * a);
		if (root < t_min || t_max < root) {
			return false;
		}
	}
	rec.t = root;
	rec.p = r.at(rec.t);
	vec3 outward_normal = (rec.p - center) / radius; 
	rec.set_face_normal(r, outward_normal);
	rec.mat_ptr = mat_ptr;
	//rec.T = normalize(vec3(-rec.p.y, rec.p.x, 0));
	//rec.B = normalize(cross(rec.normal, rec.T));

	return true;
}

void PT::hitRecord::set_face_normal(Ray& r, vec3& outward_normal) {
	this->front_face = dot(r.dir, outward_normal) < 0;
	this->normal = front_face ? outward_normal : -outward_normal;
}
PT::hittable_list::hittable_list(hittable* object) {
	add(object);
}

void PT::hittable_list::clear() {
	objects.clear();
}

void PT::hittable_list::add(hittable* object) {
	objects.push_back(object);
}

bool PT::hittable_list::hit(Ray& r, double t_min, double t_max, hitRecord& rec) {
	hitRecord temp_rec; 
	bool hit_anything = false;
	auto closest = t_max;
	for (hittable* object : objects) {
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

