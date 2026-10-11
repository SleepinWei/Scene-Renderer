#include "atmosphere-types.glsl"
#ifndef AERIAL_SET
#define AERIAL_SET 0
#define AERIAL_BINDING 0
#endif
layout(set=AERIAL_SET,binding=AERIAL_BINDING,std140) uniform AerialParameters {
    AtmosphereParameters atmosphere;
    vec4 aerialSunMultiple; // towards sun, multiple scattering scale
    vec4 aerialSolar;       // top-of-atmosphere irradiance, enabled
    vec4 aerialDomain;      // sea level m, start m, maximum distance m, optical distance scale
    vec4 aerialVisibility; // directional sun index, volume shadow switch, reserved
};
const ivec4 dimensions=ivec4(32,32,256,64);
#include "atmosphere-functions.glsl"
