layout(set=0,binding=0,std140) uniform OceanParameters {
    ivec4 grid;
    vec4 spectrumData;
    vec4 windData;
    vec4 displacementData;
    vec4 previousWindData; // previous XZ velocity, blend, normalized source amplitude
    vec4 seaStateData; // enabled, uniform depth (0 deep), wind period, gamma
    vec4 spreadingData; // wind spread, swell spread, swell period
    vec4 swellData; // XZ propagation direction, normalized amplitude
};
#define N grid.x
#define Ns grid.y
#define Seed grid.w
#define Time spectrumData.x
#define A spectrumData.y
#define OceanLength spectrumData.z
#define WindAndSeed windData
#define Lambda displacementData.x
#define HeightScale displacementData.y
#define BubblesScale displacementData.z
#define BubblesThreshold displacementData.w
