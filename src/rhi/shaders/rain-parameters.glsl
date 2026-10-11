#ifdef RAIN_FRAGMENT
#define RAIN_BINDING 2
#else
#define RAIN_BINDING 0
#endif
layout(set=0,binding=RAIN_BINDING,std140) uniform RainParameters {
    mat4 rainVP;
    vec4 rainCameraTime, rainWindRate, rainScreen, rainColour;
};
