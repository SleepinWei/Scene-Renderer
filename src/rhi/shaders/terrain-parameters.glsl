layout(set=0,binding=0,std140) uniform TerrainParameters {mat4 viewProjection;mat4 view;mat4 model;ivec4 terrainCounts;ivec4 heightDimensions;};
#define currentLOD uint(terrainCounts.x)
layout(set=1,binding=6,std430) readonly buffer HeightField {float heightSamples[];};
float sampleHeight(vec2 uv){vec2 p=clamp(uv,0.,1.)*vec2(heightDimensions.xy-1);ivec2 a=ivec2(floor(p)),b=min(a+1,heightDimensions.xy-1);vec2 f=fract(p);return mix(mix(heightSamples[a.y*heightDimensions.x+a.x],heightSamples[a.y*heightDimensions.x+b.x],f.x),mix(heightSamples[b.y*heightDimensions.x+a.x],heightSamples[b.y*heightDimensions.x+b.x],f.x),f.y);}
