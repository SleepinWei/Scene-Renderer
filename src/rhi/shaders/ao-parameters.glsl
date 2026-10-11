layout(set=0,binding=0,std140) uniform EffectsParameters {
    mat4 effectView;
    mat4 effectProjection;
    vec4 aoSettings; // world radius, world bias, power, enabled
    vec4 aoOptions; // horizon method, slices, steps per side, denoise
    mat4 aoInverseProjection;
    vec4 aoDepthOptions; // linear reconstruction, conservative hierarchy, mip count, standard projection fast path
};
