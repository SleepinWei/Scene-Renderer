#pragma once
#include"../hittable.h"
#include<memory>

using std::shared_ptr;

namespace PT {
    class Ray;

    class translate : public hittable {
    public:
        translate(shared_ptr<hittable> p, const vec3& displacement);

        virtual bool hit(
            const Ray& r, double t_min, double t_max, hitRecord& rec) const override;

        virtual bool bounding_box(double time0, double time1, AABB& output_box) const override;

    public:
        shared_ptr<hittable> ptr;
        vec3 offset;
    };

    //class rotate_y : public hittable {
    //public:
    //    rotate_y(shared_ptr<hittable> p, double angle);

    //    virtual bool hit(
    //        const ray& r, double t_min, double t_max, hit_record& rec) const override;

    //    virtual bool bounding_box(double time0, double time1, aabb& output_box) const override {
    //        output_box = bbox;
    //        return hasbox;
    //    }

    //public:
    //    shared_ptr<hittable> ptr;
    //    double sin_theta;
    //    double cos_theta;
    //    bool hasbox;
    //    aabb bbox;
    //};
}
