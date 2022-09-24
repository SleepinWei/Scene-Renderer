#pragma once
#include"PathTracing.h"
#include"PTVec.h"
namespace PT {
    class Camera {
    public:
        Camera(
            vec3 lookfrom,
            vec3 lookat,
            vec3 up,
            double fov,
            double aspect_ratio
        );
        Ray get_ray(double u, double v)const;

    public:
        vec3 origin;
        vec3 lower_left_corner;
        vec3 horizontal;
        vec3 vertical;

        double aspect_ratio;
        double viewport_height;
        double viewport_width;
        double focal_length;
    };
}
