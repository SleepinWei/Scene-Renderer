#include"PT/hittable/medium.h"
#include<limits>
#include"PT/PTAABB.h"
#include"PT/PTRay.h"
#include"PT/PTTexture.h"
#include"PT/PTMaterial.h"
#include<iostream>
using namespace PT;

using std::make_shared;

bool Medium::hit(
	const Ray& r, double t_min, double t_max, hit_record& rec) const {
    const bool enableDebug = false;
    const bool debugging = enableDebug && random_double() < 0.00001;

    hit_record rec1, rec2;

    const double infinity = std::numeric_limits<float>::infinity();

    if (!boundary->hit(r, -infinity, infinity, rec1))
        return false;

    if (!boundary->hit(r, rec1.t + 0.0001, infinity, rec2))
        return false;

    if (debugging) std::cerr << "\nt_min=" << rec1.t << ", t_max=" << rec2.t << '\n';

    if (rec1.t < t_min) rec1.t = t_min;
    if (rec2.t > t_max) rec2.t = t_max;

    if (rec1.t >= rec2.t)
        return false;

    if (rec1.t < 0)
        rec1.t = 0;

    const auto ray_length = glm::length(r.dir);
    const auto distance_inside_boundary = (rec2.t - rec1.t) * ray_length;
    const auto hit_distance = this->neg_inv_density * log(random_double());

    if (hit_distance > distance_inside_boundary)
        return false;

    rec.t = rec1.t + hit_distance / ray_length;
    rec.p = r.at(rec.t);

    //if (debugging) {
        //std::cerr << "hit_distance = " << hit_distance << '\n'
            //<< "rec.t = " << rec.t << '\n'
            //<< "rec.p = " << rec.p << '\n';
    //}

    rec.normal = vec3(1, 0, 0);  // arbitrary
    rec.front_face = true;     // also arbitrary
    rec.mat_ptr = phase_function;

    return true;
}


bool Medium::bounding_box(double time0, double time1, AABB& output_box) const {
	return this->boundary->bounding_box(time0, time1, output_box);
}

Medium::Medium(shared_ptr<hittable> bound_, float d, shared_ptr<Texture> a)
	:boundary(bound_),
	neg_inv_density(-1.0f/d),
	phase_function(make_shared<Isotropic>(a))
{
	
}
Medium::Medium(shared_ptr<hittable> bound_, float d, vec3 color)
	:boundary(bound_),
	neg_inv_density(-1.0f/d),
	phase_function(make_shared<Isotropic>(color))
{

}

Medium::~Medium() {

}