// Shared by count and generation: rejected vegetation never consumes the budget.
layout(set=0,binding=0,std140) uniform GrassParameters {
    mat4 terrainModel;ivec4 heightDimensions;vec4 grassSettings;
    vec4 placement;vec4 shape;vec4 controls;vec4 grassCamera;
};
layout(set=0,binding=1,std140) uniform GrassSurface {mat4 surfaceView;mat4 surfaceVP;vec4 surfaceScreen;};
layout(set=1,binding=4,rgba32f) readonly uniform image2D lodMap;
layout(set=1,binding=0,std140) readonly buffer FinalNodeList {uint finalNodeCnt;uint one;uint one_;uvec3 finalNode[];};
layout(set=1,binding=5) uniform sampler2D waterMask;
layout(set=1,binding=6) uniform sampler2D heightAtlas;
layout(set=1,binding=7) uniform sampler2D heightPageTable;
#include "virtual-texture.glsl"
float sampleHeight(vec2 uv){return virtualSampleLevel(heightAtlas,heightPageTable,uv,0).r;}
#include "terrain-surface.glsl"
uint hash(uint v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
float random(uint seed,uint salt){return float(hash(seed^salt)>>8)*(1./16777216.);}
float cross2(vec2 a,vec2 b){return a.x*b.y-a.y*b.x;}
bool attachTriangle(vec2 point,vec3 a,vec3 b,vec3 c,out float height,out vec3 normal){
    float area=cross2(b.xz-a.xz,c.xz-a.xz);if(abs(area)<1e-12)return false;
    float wb=cross2(point-a.xz,c.xz-a.xz)/area,wc=cross2(b.xz-a.xz,point-a.xz)/area;
    if(wb<-.0001 || wc<-.0001 || wb+wc>1.0001)return false;
    height=a.y*(1.-wb-wc)+b.y*wb+c.y*wc;
    vec3 edgeA=mat3(terrainModel)*(b-a),edgeB=mat3(terrainModel)*(c-a);
    normal=normalize(cross(edgeA,edgeB));return true;
}
bool leafEligible(){
    uint leaf=gl_WorkGroupID.x;if(leaf>=finalNodeCnt)return false;
    uvec3 node=finalNode[leaf];if(node.z>uint(controls.x))return false;
    float span=float(8u<<node.z)/1280.;vec2 uv=(vec2(node.xy)+.5)*span;
    vec3 center=(terrainModel*vec4(uv.x*2.-1.,0,uv.y*2.-1.,1)).xyz;
    float radius=span*(length(terrainModel[0].xyz)+length(terrainModel[2].xyz));
    return distance(center.xz,grassCamera.xz)<grassSettings.w+radius;
}
// A 16x16 group shares four corners for each of its 8x8 cells. Dense
// patches reuse these instead of repeating VT/morph evaluation per blade.
shared vec3 patchCorners[256];
void preparePatch(){
    uint index=gl_LocalInvocationIndex,cell=index/4u;
    uvec3 node=finalNode[gl_WorkGroupID.x];int interval=int(1u<<node.z);
    ivec2 offsets[4]={ivec2(0,0),ivec2(1,0),ivec2(1,1),ivec2(0,1)};
    ivec2 grid=(ivec2(node.xy)*8+ivec2(int(cell)%8,int(cell)/8))*interval;
    patchCorners[index]=terrainVertex(grid+offsets[index%4u]*interval,interval,terrainModel,surfaceView,surfaceVP,surfaceScreen);
    barrier();
}
bool candidate(uint sampleIndex,out vec3 position,out uint seed,out float growth){
    uint leaf=gl_WorkGroupID.x;if(leaf>=finalNodeCnt)return false;
    uvec3 node=finalNode[leaf];if(node.z>uint(controls.x))return false;
    uint blocks=uint(controls.z)/2u;
    uvec2 cell=gl_LocalInvocationID.xy/2,subcell=gl_LocalInvocationID.xy%2+2u*uvec2(sampleIndex%blocks,sampleIndex/blocks);
    int interval=int(1u<<node.z);ivec2 grid=(ivec2(node.xy)*8+ivec2(cell))*interval;
    // Integer lattice identity does not change when a height VT page arrives.
    seed=hash(uint(grid.x)*65537u+uint(grid.y)*17u+subcell.x*7919u+subcell.y*104729u);
    vec2 at=(vec2(grid)+(vec2(subcell)+vec2(random(seed,1u),random(seed,2u)))/controls.z*float(interval))/1280.;
    vec2 point=at*2.-1.;position=(terrainModel*vec4(point.x,0,point.y,1)).xyz;
    float horizontalDistance=length(position.xz-grassCamera.xz);
    if(horizontalDistance>=grassSettings.w || grassSettings.z<=0.)return false;
    float fade=1.-smoothstep(placement.x,grassSettings.w,horizontalDistance);
    if(random(seed,3u)>=grassSettings.z*fade)return false;
    if(shape.w>.5 && texture(waterMask,vec2(at.x,1.-at.y)).r>.1)return false;
    vec3 corners[4];uint base=(cell.y*8u+cell.x)*4u;
    for(uint i=0;i<4u;i++)corners[i]=patchCorners[base+i];
    float attachedHeight;vec3 normal;
    if(!attachTriangle(point,corners[0],corners[2],corners[1],attachedHeight,normal) &&
       !attachTriangle(point,corners[0],corners[3],corners[2],attachedHeight,normal))return false;
    position=(terrainModel*vec4(point.x,attachedHeight,point.y,1)).xyz;
    if(abs(normal.y)<placement.y || position.y<=placement.z+placement.w || position.y>shape.z)return false;
    float radius=max(shape.x*.3,shape.y*.15);
    if(controls.y>.5){
        vec4 clip=surfaceVP*vec4(position+vec3(0,radius*.5,0),1);
        vec2 extent=radius*vec2(length(vec3(surfaceVP[0][0],surfaceVP[1][0],surfaceVP[2][0])),length(vec3(surfaceVP[0][1],surfaceVP[1][1],surfaceVP[2][1])));
        if(clip.w<=0. || abs(clip.x)>clip.w+extent.x || abs(clip.y)>clip.w+extent.y || clip.z < -clip.w-radius || clip.z>clip.w+radius)return false;
    }
    growth=(.75+.5*random(seed,4u))*smoothstep(0.,.15,fade);
    return true;
}
