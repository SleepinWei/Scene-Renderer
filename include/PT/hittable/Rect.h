#pragma once
#include"PT/hittable.h"
#include"PT/glm_vec3.h"
#include<PT/PTrandom.h>
#include<PT/PTRay.h>
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

        virtual bool hit(const Ray& r, double t_min, double t_max, hit_record& rec) const override;
        virtual bool bounding_box(double time0, double time1, AABB& output_box) const override;



    public:
        //glm::mat4 model;
        shared_ptr<Material> mp;
        double x0, x1, y0, y1, k;
    };

    class xz_rect : public hittable {
    public:
        xz_rect();

        xz_rect(double _x0, double _x1, double _z0, double _z1, double _k,
            shared_ptr<Material> mat);

        virtual bool hit(const Ray& r, double t_min, double t_max, hit_record& rec) const override;

        virtual bool bounding_box(double time0, double time1, AABB& output_box) const override;

        virtual double pdf_value(const vec3& origin, const vec3& v) const override {
            hit_record rec;
            if (!this->hit(Ray(origin, v), 0.001, INFINITY, rec))
                return 0;

            auto area = (x1-x0)*(z1-z0);
            auto distance_squared = rec.t * rec.t * glm::dot(v, v);
            auto cosine = fabs(dot(v, rec.normal) / v.length());

            return distance_squared / (cosine * area);
        }

        virtual vec3 random(const vec3& origin) const override {
            auto random_point = vec3(random_double(x0,x1), k, random_double(z0,z1));
            return random_point - origin;
        }
    public:
        //glm::mat4 model;
        shared_ptr<Material> mp;
        double x0, x1, z0, z1, k;
    };

    class yz_rect : public hittable {
    public:
        yz_rect();

        yz_rect(double _y0, double _y1, double _z0, double _z1, double _k,
            shared_ptr<Material> mat);

        virtual bool hit(const Ray& r, double t_min, double t_max, hit_record& rec) const override;

        virtual bool bounding_box(double time0, double time1, AABB& output_box) const override;
    
        //virtual void setModel(glm::mat4 model);

    public:
        //glm::mat4 model;
        shared_ptr<Material> mp;
        double y0, y1, z0, z1, k;
    };
}
