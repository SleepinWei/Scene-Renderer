layout(set=0,binding=0,std140) uniform GeometryCullParameters {
    mat4 cullViewProjection;
    mat4 cullModel;
    vec4 cullGuard; // lateral expansion, relative roundoff, unused.
    uvec4 cullCounts; // triangle capacity, vertex capacity, indirect input, groups.
};
