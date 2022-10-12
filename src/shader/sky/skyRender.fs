#version 450 core

struct DensityProfileLayer{
    float width; // an atomosphere layer of width 'width'
    float exp_term;
    float exp_scale; 
    float linear_term; 
    float const_term; 
};

// two layers of atmosphere
struct DensityProfile{
    DensityProfileLayer layers[2]; 
};

struct AtmosphereParameters{
    float solar_irradiance; // solar irradiance at the top of the atmosphere
    float sun_angular_radius; 
    float top_radius;
    float bottom_radius; 

    DensityProfile rayleigh_density; 
    vec3 rayleigh_scattering;//scattering coefficient of air molecules
    DensityProfile mie_density; 
    vec3 mie_scattering; // scattering cofficient, a function of wavelength.
    vec3 mie_extinction; 
    float mie_g; // g coefficient in phase function

    DensityProfile absorption_density; //ozone layer
    vec3 absorption_extinction; 

    vec3 ground_albedo; // average albedo (can be replaced by gorund textures? perhaps)

    // cosine of maximum sun zenith angle. 
    // which is the smallest sun zenith angle yielding negligible 
    // sky light radiance values. Earth: 102 degress -> -0.2 
    float mu_s_min;
};

// layout(rgba32f,binding=0) uniform image2D transmittance_texture;
// layout(rgba32f,binding=0) uniform image2D skyViewLut;
uniform sampler2D transmittance_texture;
uniform sampler2D skyViewLut;

layout(std140, binding=7) uniform ATMOS{
    AtmosphereParameters atmosphere;
};

#define PI 3.1415926
float MapLatitudeToUnit(float latitude){
    return 0.5f + 0.5f * sign(latitude) * sqrt(abs(latitude)/(PI/2));
}

float MapUnitToLatitude(float unit){
    float s = sign(unit - 0.5f);
    return ((2 * unit-1.0f) * (2*unit-1.0f)) * s * PI / 2;
}

out vec4 FragColor;
in vec3 viewDirection; 

void main(){
    vec3 viewDir = normalize(viewDirection);
    float sinLat = length(cross(viewDir,vec3(0.0f, 1.0f, 0.0f)));
    vec3 projected = vec3(viewDir.x,0.0f,viewDir.z);
    float cosLon = dot(normalize((projected)),
        vec3(0.0f,0.0f,-1.0f));
    float Lat = asin(sinLat);
    float Lon = acos(cosLon);
    float LatUnit = MapLatitudeToUnit(Lat);
    float LonUnit = Lon / (PI);
    vec4 color = texture(skyViewLut,vec2(LonUnit,LatUnit));
    FragColor = color;
}