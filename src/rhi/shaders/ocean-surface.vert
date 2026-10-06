#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 aPos;layout(location=1) in vec2 aTexCoord;
layout(set=0,binding=0,std140) uniform WaterVertex {mat4 waterVP;mat4 model;mat4 previousVP;mat4 previousView;mat4 previousModel;ivec4 vertexFlags;vec4 vertexSettings;vec4 grid;vec4 previousGrid;mat4 coastInverse;mat4 coastModel;vec4 coastPatch;vec4 coastPreviousPatch;vec4 coastFeatures;vec4 coastBedInfo;};
layout(set=0,binding=2) uniform sampler2D DisplaceRT;
layout(set=0,binding=3) uniform sampler2D detailDisplace;
layout(set=0,binding=4) uniform sampler2D previousDisplace;
layout(set=0,binding=5) uniform sampler2D previousDetailDisplace;
layout(set=0,binding=6) uniform sampler2D vertexShoreState;
layout(set=0,binding=7) uniform sampler2D previousShoreState;
layout(location=0) out vec3 FragPos;layout(location=1) out vec2 FragTexCoord;
layout(location=2) out vec2 WaterMaskCoord;
layout(location=5) out vec2 DetailTexCoord;layout(location=6) out vec4 PreviousClip;layout(location=7) out float PreviousDepth;
#include "water-waves.glsl"
#include "water-coast.glsl"
float focusCoordinate(float u,float centre,float domain,float focus) {
    if(focus<=0)return (u-.5)*domain;
    centre=clamp(centre,-domain*.5,domain*.5);
    return centre+focus*sinh(mix(asinh((-domain*.5-centre)/focus),asinh((domain*.5-centre)/focus),u));
}
vec2 gridPosition(vec2 uv,vec4 settings) {
    return vec2(focusCoordinate(uv.x,settings.x,settings.z,settings.w),focusCoordinate(uv.y,settings.y,settings.z,settings.w));
}
vec2 gridUV(vec2 xz,vec4 settings) {
    if(settings.w<=0)return xz/settings.z+.5;
    vec2 centre=clamp(settings.xy,vec2(-settings.z*.5),vec2(settings.z*.5));
    vec2 lo=asinh((vec2(-settings.z*.5)-centre)/settings.w),hi=asinh((vec2(settings.z*.5)-centre)/settings.w);
    return (asinh((xz-centre)/settings.w)-lo)/(hi-lo);
}
vec3 displacedPosition(vec2 uv,vec4 settings,sampler2D large,sampler2D detail,sampler2D shore,vec4 bounds,float level) {
    vec2 xz=gridPosition(uv,settings);float du=1./max(vertexSettings.z-1.,1.);
    // Local world footprint filters unresolved displacement; detail normals
    // still shade per pixel. The full water domain keeps fixed boundaries.
    vec2 stepX=gridPosition(clamp(uv+vec2(du,0),vec2(0),vec2(1)),settings)-gridPosition(clamp(uv-vec2(du,0),vec2(0),vec2(1)),settings);
    vec2 stepZ=gridPosition(clamp(uv+vec2(0,du),vec2(0),vec2(1)),settings)-gridPosition(clamp(uv-vec2(0,du),vec2(0),vec2(1)),settings);
    float footprint=max(length(stepX),length(stepZ))*.5;
    vec4 waves=vec4(0,vertexSettings.y,vertexSettings.x,0);
    vec3 displacement=waterDisplacement(xz,large,detail,waves);
    if(footprint>vertexSettings.y/float(textureSize(large,0).x)){
        vec2 dx=vec2(footprint*.5,0),dz=dx.yx;
        displacement=(displacement*2.+waterDisplacement(xz+dx,large,detail,waves)+waterDisplacement(xz-dx,large,detail,waves)+waterDisplacement(xz+dz,large,detail,waves)+waterDisplacement(xz-dz,large,detail,waves))/6.;
    }
    float macroHeight=displacement.y+level;
    if(vertexFlags.x!=0)displacement+=textureLod(detail,waterWaveUV(xz,vertexSettings.x,detail),0).xyz*(1.-smoothstep(.25,1.,footprint))*coastDetailWeight(xz+displacement.xz,shore,bounds,level,coastFeatures.z);
    // Detail is already depth attenuated; replace only the macro contribution.
    displacement.y=coastHeight(xz+displacement.xz,macroHeight,shore,bounds,level,coastFeatures.z)-level+(displacement.y+level-macroHeight);
    return vec3(xz.x,0,xz.y)+displacement;
}
void main() {
    vec2 xz=gridPosition(aTexCoord,grid);
    WaterMaskCoord=xz/grid.z+.5;
    FragTexCoord=waterWaveUV(xz,vertexSettings.y,DisplaceRT);
    DetailTexCoord=waterWaveUV(xz,vertexSettings.x,detailDisplace);
    FragPos=(model*vec4(displacedPosition(aTexCoord,grid,DisplaceRT,detailDisplace,vertexShoreState,coastPatch,model[3].y),1)).xyz;
    gl_Position=waterVP*vec4(FragPos,1);
    PreviousClip=gl_Position;PreviousDepth=0;
    // Remeshing relocates vertices, not the water material: history tracks the
    // same world-space wave coordinate rather than a moved grid index.
    if(vertexFlags.y!=0){vec4 oldPosition=previousModel*vec4(displacedPosition(gridUV(xz,previousGrid),previousGrid,previousDisplace,previousDetailDisplace,previousShoreState,coastPreviousPatch,previousModel[3].y),1);
        PreviousClip=previousVP*oldPosition;PreviousDepth=-(previousView*oldPosition).z;}
}
