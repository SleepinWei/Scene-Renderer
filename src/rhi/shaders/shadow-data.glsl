#ifndef SHADOW_SET
#define SHADOW_SET 0
#define SHADOW_BINDING 3
#endif
layout(set=SHADOW_SET,binding=SHADOW_BINDING,std140) uniform ShadowData {
    mat4 shadowCameraView;mat4 shadowMatrices[180];vec4 shadowRects[180];ivec4 shadowLights[30];vec4 shadowSplits[30];vec4 shadowSettings;
    mat4 rsmMatrix;vec4 rsmRect;vec4 rsmSettings;
    vec4 shadowLightDepth[30];vec4 shadowFilter;vec4 shadowCascades;
};
