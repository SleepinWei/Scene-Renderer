#ifndef RHI_LIGHT_DATA
#define RHI_LIGHT_DATA
struct Light { vec4 positionType; vec4 colorInner; vec4 directionOuter; };
layout(set=0,binding=2,std140) uniform SceneLighting { vec4 cameraAmbient; ivec4 counts; Light lights[30]; };
#endif
