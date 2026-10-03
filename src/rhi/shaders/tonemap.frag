#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std140) uniform ToneMap { vec4 exposureGamma; };
layout(set=0,binding=1) uniform sampler2D hdrBuffer;
void main() {
    vec3 hdr = max(texture(hdrBuffer, uv).rgb,vec3(0));
    vec3 mapped = exposureGamma.z>0.?clamp(hdr,0.,1.):vec3(1)-exp(-hdr*exposureGamma.x);
    color = vec4(pow(mapped,vec3(1/exposureGamma.y)),1);
}
