#pragma once
#include"PathTracing.h"
#include"PTVec.h"
namespace PT {
    class Camera {
    public:
        Camera();
        Ray get_ray(double u, double v);

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
