#include<utility>
#include"PTAABB.h"
#include"PTRay.h"

using namespace PT;

PT::AABB::AABB(const vec3& a, const vec3& b):
	minimum(a),maximum(b) {
}

AABB::AABB(const AABB& box_) {
    minimum = box_.minimum;
    maximum = box_.maximum;
}

bool PT::AABB::hit(const Ray& r, double t_min, double t_max)const {
     for (int a = 0; a < 3; a++) {
        auto invD = 1.0f / r.dir[a];
        auto t0 = (minimum[a] - r.orig[a]) * invD;
        auto t1 = (maximum[a] - r.orig[a]) * invD;
        if (invD < 0.0f)
            std::swap(t0, t1);
        t_min = t0 > t_min ? t0 : t_min;
        t_max = t1 < t_max ? t1 : t_max;
        if (t_max <= t_min)
            return false;
    }
    return true;
}

AABB AABB::surrounding_box(const AABB& box0, const AABB& box1) {
    vec3 small(fmin(box0.minimum.x, box1.minimum.x),
        fmin(box0.minimum.y, box1.minimum.y),
        fmin(box0.minimum.z, box1.minimum.z));

    vec3 big(fmax(box0.maximum.x, box1.maximum.x),
        fmax(box0.maximum.y, box1.maximum.y),
        fmax(box0.maximum.z, box1.maximum.z));

    return AABB(small, big);
}
