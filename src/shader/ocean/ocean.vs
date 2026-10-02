#version 430 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord;
uniform sampler2D DisplaceRT, detailDisplace;
uniform int enableDetail;
uniform float detailLength;
layout(std140,binding=0) uniform VP { mat4 projection;mat4 view;vec3 viewPos; };
uniform mat4 model;
out vec3 FragPos;
out vec2 FragTexCoord;
layout(location=5) out vec2 DetailTexCoord;
void main() {
    // A periodic grid stores sample 0 at texel centre, not at the texture edge.
    FragTexCoord=aTexCoord+0.5/vec2(textureSize(DisplaceRT,0));
    DetailTexCoord=aPos.xz/max(detailLength,.01)+.5+.5/vec2(textureSize(detailDisplace,0));
    vec3 displacement=textureLod(DisplaceRT,FragTexCoord,0).xyz;
    if(enableDetail!=0)displacement+=textureLod(detailDisplace,DetailTexCoord,0).xyz;
    FragPos=(model*vec4(aPos+displacement,1)).xyz;
    gl_Position=projection*view*vec4(FragPos,1);
}
