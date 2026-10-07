layout(set=0,binding=0,std140) uniform SunShaftParameters {
    mat4 coastInverse;mat4 coastModel;vec4 coastPatch;vec4 coastPreviousPatch;vec4 coastFeatures;vec4 coastBedInfo;
    vec4 shaftPatch;vec4 waves;vec4 sun;vec4 shaftProjection;vec4 shaftSettings;ivec4 grid;
};
