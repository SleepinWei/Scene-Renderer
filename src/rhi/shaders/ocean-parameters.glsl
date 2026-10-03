layout(set=0,binding=0,std140) uniform OceanParameters {
    ivec4 grid;
    vec4 spectrumData;
    vec4 windData;
    vec4 displacementData;
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
