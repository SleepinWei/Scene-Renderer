#include"Rect.h"
#include"../PTRay.h"
#include"../PTAABB.h"

using namespace PT;

xy_rect::xy_rect() {}

xy_rect::xy_rect(double _x0, double _x1, double _y0, double _y1, double _k,
        shared_ptr<Material> mat)
        : x0(_x0), x1(_x1), y0(_y0), y1(_y1), k(_k), mp(mat) {
};

bool xy_rect::hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const {
    auto t = (k - r.orig.z) / r.dir.z;
    if (t < t_min || t > t_max)
        return false;
    auto x = r.orig.x + t * r.dir.x;
    auto y = r.orig.y + t * r.dir.y;
    if (x < x0 || x > x1 || y < y0 || y > y1)
        return false;
    rec.u = (x - x0) / (x1 - x0);
    rec.v = (y - y0) / (y1 - y0);
    rec.t = t;
    auto outward_normal = vec3(0, 0, 1);
    rec.set_face_normal(r, outward_normal);
    rec.mat_ptr = mp;
    rec.p = r.at(t);
    return true;
}
bool xy_rect::bounding_box(double time0, double time1, AABB& output_box) const{
	// The bounding box must have non-zero width in each dimension, so pad the Z
	// dimension a small amount.
	output_box = AABB(vec3(x0, y0, k - 0.0001), vec3(x1, y1, k + 0.0001));
	return true;
}

xz_rect::xz_rect() {}

xz_rect::xz_rect(double _x0, double _x1, double _z0, double _z1, double _k,
        shared_ptr<Material> mat)
        : x0(_x0), x1(_x1), z0(_z0), z1(_z1), k(_k), mp(mat) {};

bool xz_rect::hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const {
    auto t = (k - r.orig.y) / r.dir.y;
    if (t < t_min || t > t_max)
        return false;
    auto x = r.orig.x + t * r.dir.x;
    auto z = r.orig.z + t * r.dir.z;
    if (x < x0 || x > x1 || z < z0 || z > z1)
        return false;
    rec.u = (x - x0) / (x1 - x0);
    rec.v = (z - z0) / (z1 - z0);
    rec.t = t;
    auto outward_normal = vec3(0, 1, 0);
    rec.set_face_normal(r, outward_normal);
    rec.mat_ptr = mp;
    rec.p = r.at(t);
    return true;
}

bool xz_rect::bounding_box(double time0, double time1, AABB& output_box) const {
        // The bounding box must have non-zero width in each dimension, so pad the Y
        // dimension a small amount.
        output_box = AABB(vec3(x0, k - 0.0001, z0), vec3(x1, k + 0.0001, z1));
        return true;
    }

yz_rect::yz_rect() {}

yz_rect::yz_rect(double _y0, double _y1, double _z0, double _z1, double _k,
        shared_ptr<Material> mat)
        : y0(_y0), y1(_y1), z0(_z0), z1(_z1), k(_k), mp(mat) {};

bool yz_rect::hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const {
    auto t = (k - r.orig.x) / r.dir.x;
    if (t < t_min || t > t_max)
        return false;
    auto y = r.orig.y + t * r.dir.y;
    auto z = r.orig.z + t * r.dir.z;
    if (y < y0 || y > y1 || z < z0 || z > z1)
        return false;
    rec.u = (y - y0) / (y1 - y0);
    rec.v = (z - z0) / (z1 - z0);
    rec.t = t;
    auto outward_normal = vec3(1, 0, 0);
    rec.set_face_normal(r, outward_normal);
    rec.mat_ptr = mp;
    rec.p = r.at(t);
    return true;
}

bool yz_rect::bounding_box(double time0, double time1, AABB& output_box) const {
	// The bounding box must have non-zero width in each dimension, so pad the X
	// dimension a small amount.
	output_box = AABB(vec3(k - 0.0001, y0, z0), vec3(k + 0.0001, y1, z1));
	return true;
}
