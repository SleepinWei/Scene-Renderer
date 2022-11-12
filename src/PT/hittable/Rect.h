#pragma once
#include"../hittable.h"
#include<memory>

using std::shared_ptr;
using std::make_shared;

namespace PT {
    class Material;
    class AABB;
    class Ray;

    class xy_rect : public hittable {
    public:
        xy_rect();

        xy_rect(double _x0, double _x1, double _y0, double _y1, double _k,
            shared_ptr<Material> mat);

        virtual bool hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const override;
        virtual bool bounding_box(double time0, double time1, AABB& output_box) const override;

    public:
        shared_ptr<Material> mp;
        double x0, x1, y0, y1, k;
    };

    class xz_rect : public hittable {
    public:
        xz_rect();

        xz_rect(double _x0, double _x1, double _z0, double _z1, double _k,
            shared_ptr<Material> mat);

        virtual bool hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const override;

        virtual bool bounding_box(double time0, double time1, AABB& output_box) const override;

    public:
        shared_ptr<Material> mp;
        double x0, x1, z0, z1, k;
    };

    class yz_rect : public hittable {
    public:
        yz_rect();

        yz_rect(double _y0, double _y1, double _z0, double _z1, double _k,
            shared_ptr<Material> mat);

        virtual bool hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const override;

        virtual bool bounding_box(double time0, double time1, AABB& output_box) const override;

    public:
        shared_ptr<Material> mp;
        double y0, y1, z0, z1, k;
    };
}
