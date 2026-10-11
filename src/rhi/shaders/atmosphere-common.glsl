#include "atmosphere-types.glsl"
layout(set=0,binding=0,std140) uniform ATMOS {AtmosphereParameters atmosphere;};
layout(set=0,binding=1,std140) uniform LutParameters {
    ivec4 dimensions;vec4 sunDirectionHeight;vec4 solarColorMultiple;vec4 lutSettings;
};
#include "atmosphere-functions.glsl"
