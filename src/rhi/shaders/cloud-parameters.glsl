layout(set=0,binding=0,std140) uniform CloudParameters {
    mat4 cloudInverseVP,cloudPreviousVP; // first matrix unprojects into camera-relative coordinates
    vec4 cloudCameraTime,cloudPreviousCameraTime;
    vec4 cloudLayer; // base, thickness, coverage, extinction per meter
    vec4 cloudShape; // shape period, weather period, erosion, max distance
    vec4 cloudWindHistory; // wind x/z, history valid, history weight
    vec4 cloudSun,cloudSunColor;
    vec4 cloudPlanet; // planet radius, sea level, unused, unused
    vec4 cloudVolumeCenterMode,cloudVolumeSize,cloudVolumeQuality;
    ivec4 cloudGrid; // width,height,tile columns,frame sample
    ivec4 cloudQuality; // march steps,light steps,seed,downsample
};
