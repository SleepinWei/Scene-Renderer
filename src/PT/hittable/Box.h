#pragma once
#include<glm/glm.hpp>
#include<memory>

#include"../hittable.h"

using glm::vec3;
using point3 = glm::vec3;
using std::shared_ptr;

namespace PT {
    class Material;
    class Ray;
    class hitRecord;
    class AABB;

    class Box: public hittable {
    public:
        Box();
		Box(const point3& p0, const point3& p1, shared_ptr<Material> ptr);

        void setModel(glm::mat4 model) override;

        virtual bool hit(const Ray& r, double t_min, double t_max, hitRecord& rec) const override;

        virtual bool bounding_box(double time0, double time1, AABB& output_box) const override;
    public:
        // TODO: add model transformation to all;
        glm::mat4 model;

        vec3 box_min;
        vec3 box_max;
        hittable_list sides;
    };
}
