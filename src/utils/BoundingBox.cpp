#include<utility>
#include"BoundingBox.h"

AABB::AABB(const vec3& a, const vec3& b):
	minimum(a),maximum(b) {
}

//AABB::AABB(const AABB& box_) {
//    minimum = box_.minimum;
//    maximum = box_.maximum;
//}

AABB AABB::surrounding_box(const AABB& box0, const AABB& box1) {
    vec3 small(fmin(box0.minimum.x, box1.minimum.x),
        fmin(box0.minimum.y, box1.minimum.y),
        fmin(box0.minimum.z, box1.minimum.z));

    vec3 big(fmax(box0.maximum.x, box1.maximum.x),
        fmax(box0.maximum.y, box1.maximum.y),
        fmax(box0.maximum.z, box1.maximum.z));

    return AABB(small, big);
}

BoundingSphere::BoundingSphere(const vec3& _center, float _radius) {
    center = _center;
    this->radius = _radius;
    initDone = false;
}

BoundingSphere::BoundingSphere(const BoundingSphere& s) {
    center = s.center;
    radius = s.radius;
    initDone = false;
}

BoundingSphere BoundingSphere::surrounding_sphere(const BoundingSphere& s1, const BoundingSphere& s2) {
    float cc = glm::length(s1.center - s2.center);// center to center
    if (cc < s1.radius - s2.radius) {
        return BoundingSphere(s1);
    }
    else if (cc < s2.radius - s1.radius) {
        return BoundingSphere(s2);
    }
    float ratio = (cc - s1.radius + s2.radius) / (2 * cc);
    vec3 newCenter = s1.center * (1.0f - ratio) + s2.center * ratio;
    float newRadius = (cc + s1.radius+ s2.radius) / 2.0f;
    return BoundingSphere(newCenter, newRadius);
}

BoundingSphere::BoundingSphere() {
    initDone = false;
    center = vec3(0.0f);
    radius = 0.0f;
}
