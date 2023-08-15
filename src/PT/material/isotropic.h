#pragma once
#include"PT/PTMaterial.h"
using std::shared_ptr;

namespace PT {
    class Texture;
    class Ray;
    class hit_record;

    class Isotropic : public Material {
    public:
        Isotropic(vec3 c);
        Isotropic(shared_ptr<Texture> a);

        virtual bool scatter(
            const Ray& r_in, const hit_record& rec, vec3& attenuation, Ray& scattered
        ) const override;

    public:
        shared_ptr<Texture> albedo;
    };
}
