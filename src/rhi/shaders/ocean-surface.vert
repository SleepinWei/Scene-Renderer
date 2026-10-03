#version 450
layout(location=0) in vec3 aPos;layout(location=1) in vec2 aTexCoord;
layout(set=0,binding=0,std140) uniform WaterVertex {mat4 waterVP;mat4 model;mat4 previousVP;mat4 previousView;mat4 previousModel;ivec4 vertexFlags;vec4 vertexSettings;};
#define enableDetail vertexFlags.x
#define temporalActive vertexFlags.y
#define detailLength vertexSettings.x
layout(set=0,binding=2) uniform sampler2D DisplaceRT;
layout(set=0,binding=3) uniform sampler2D detailDisplace;
layout(set=0,binding=4) uniform sampler2D previousDisplace;
layout(set=0,binding=5) uniform sampler2D previousDetailDisplace;
layout(location=0) out vec3 FragPos;layout(location=1) out vec2 FragTexCoord;
layout(location=2) out vec2 WaterMaskCoord;
layout(location=5) out vec2 DetailTexCoord;layout(location=6) out vec4 PreviousClip;layout(location=7) out float PreviousDepth;
void main() {
    // A periodic grid stores sample 0 at texel centre, not at the texture edge.
    WaterMaskCoord=aTexCoord;
    FragTexCoord=aPos.xz/max(vertexSettings.y,.01)+.5+0.5/vec2(textureSize(DisplaceRT,0));
    DetailTexCoord=aPos.xz/max(detailLength,.01)+.5+.5/vec2(textureSize(detailDisplace,0));
    vec3 displacement=textureLod(DisplaceRT,FragTexCoord,0).xyz;
    if(enableDetail!=0)displacement+=textureLod(detailDisplace,DetailTexCoord,0).xyz;
    FragPos=(model*vec4(aPos+displacement,1)).xyz;
    gl_Position=waterVP*vec4(FragPos,1);
    PreviousClip=gl_Position;PreviousDepth=0;
    if(temporalActive!=0) {
        vec3 oldDisplacement=textureLod(previousDisplace,FragTexCoord,0).xyz;
        if(enableDetail!=0)oldDisplacement+=textureLod(previousDetailDisplace,DetailTexCoord,0).xyz;
        vec4 oldPosition=previousModel*vec4(aPos+oldDisplacement,1);
        PreviousClip=previousVP*oldPosition;PreviousDepth=-(previousView*oldPosition).z;
    }
}
