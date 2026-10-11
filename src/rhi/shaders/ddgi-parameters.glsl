#ifndef DDGI_SET
#define DDGI_SET 0
#endif
#ifndef DDGI_BINDING
#define DDGI_BINDING 0
#endif
layout(set=DDGI_SET,binding=DDGI_BINDING,std140) uniform DdgiParameters {
    vec4 ddgiOrigin;ivec4 ddgiCounts;vec4 ddgiBias;vec4 ddgiTemporal;
    vec4 ddgiCapture;vec4 ddgiProbe;vec4 ddgiCamera;
};
