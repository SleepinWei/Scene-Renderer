const float ATM_PI=3.141592653589793;
struct AtmosphereParameters {
    float solar_irradiance;float sun_angular_radius;float top_radius;float bottom_radius;
    float HDensityRayleigh;float HDensityMie;float OzoneCenter;float mie_g;
    vec3 rayleigh_scattering;vec3 mie_scattering;vec3 mie_extinction;vec3 absorption_extinction;float OzoneWidth;
};
