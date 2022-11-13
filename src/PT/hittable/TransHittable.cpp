#include"TransHittable.h"
#include"../PTRay.h"
#include"../PTAABB.h"

using namespace PT;
translate::translate(shared_ptr<hittable> p, const vec3& displacement)
	: ptr(p), offset(displacement) {}

bool translate::hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const {
	Ray moved_r(r.orig - offset, r.dir);
	if (!ptr->hit(moved_r, t_min, t_max, rec))
		return false;

	rec.p += offset;
	rec.set_face_normal(moved_r, rec.normal);

	return true;
}

bool translate::bounding_box(double time0, double time1, AABB& output_box) const {
	if (!ptr->bounding_box(time0, time1, output_box))
		return false;

	output_box = AABB(
		output_box.min() + offset,
		output_box.max() + offset);

	return true;
}

