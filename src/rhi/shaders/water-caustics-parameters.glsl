layout(set=0,binding=0,std140) uniform CausticParameters {
    mat4 coastInverse;mat4 coastModel;vec4 coastPatch;vec4 coastPreviousPatch;vec4 coastFeatures;vec4 coastBedInfo;
    vec4 photonPatch;vec4 waves;vec4 sun;vec4 extinction;vec4 controls;vec4 receiverProjection;vec4 sourceSettings;ivec4 grid;
};
