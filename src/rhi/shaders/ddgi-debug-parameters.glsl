#ifndef DDGI_DEBUG_SET
#define DDGI_DEBUG_SET 0
#endif
layout(set=DDGI_DEBUG_SET,binding=0,std140) uniform DdgiDebug {
    mat4 debugVP;
    vec4 debugOrigin;
    ivec4 debugCounts;
    vec4 debugRight;
    vec4 debugUp;
    vec4 debugEye;
    vec4 debugDisplay; // exposure, gamma, mode, xray
    vec4 debugJitter; // raster jitter XY, tone mapping, unused
};
