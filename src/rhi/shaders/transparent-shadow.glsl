#define SHADOW_SET 2
#define SHADOW_BINDING 1
#include "shadow-data.glsl"
layout(set=2,binding=4) uniform sampler2D shadowAtlas;
#include "shadow-visibility.glsl"
