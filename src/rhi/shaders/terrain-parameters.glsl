layout(set=0,binding=0,std140) uniform TerrainParameters {mat4 viewProjection;mat4 view;mat4 model;ivec4 terrainCounts;ivec4 heightDimensions;vec4 terrainScreen;};
#define currentLOD uint(terrainCounts.x)
layout(set=1,binding=6) uniform sampler2D heightAtlas;
layout(set=0,binding=1) uniform sampler2D heightPageTable;
#include "virtual-texture.glsl"
float sampleHeight(vec2 uv){return virtualSampleLevel(heightAtlas,heightPageTable,uv,0).r;}
