#pragma once
#include"../PTMaterial.h"
using std::shared_ptr;

namespace PT {
    class Texture;
    class Ray;
    class hitRecord;

    class Isotropic : public Material {
    public:
        Isotropic(vec3 c);
        Isotropic(shared_ptr<Texture> a);

        virtual bool scatter(
            const Ray& r_in, const hitRecord& rec, vec3& attenuation, Ray& scattered
        ) const override;

    public:
        shared_ptr<Texture> albedo;
    };
}
