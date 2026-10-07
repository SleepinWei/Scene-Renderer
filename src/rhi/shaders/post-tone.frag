#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std140) uniform PostToneParameters {
    vec4 exposureGamma; // exposure,gamma,bypass,mapper
    vec4 grading; // saturation,contrast,temperature,tint
    vec4 effects; // bloom strength,grading enabled,unused,unused
};
layout(set=0,binding=1) uniform sampler2D sourceColor;
layout(set=0,binding=2) uniform sampler2D bloomColor;
void main(){
    vec3 hdr=max(texture(sourceColor,uv).rgb,vec3(0));
    if(effects.x>0.)hdr+=texture(bloomColor,uv).rgb*effects.x;
    if(effects.y>.5)hdr*=exp2(vec3(grading.z*.25+grading.w*.125,-grading.w*.125,-grading.z*.25+grading.w*.125));
    vec3 mapped;
    if(exposureGamma.z>.5)mapped=clamp(hdr,0.,1.);
    else {
        vec3 x=hdr*exposureGamma.x;
        if(exposureGamma.w<.5)mapped=vec3(1)-exp(-x);
        else if(exposureGamma.w<1.5){x=min(x,vec3(1000));mapped=clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.);}
        else if(exposureGamma.w<2.5)mapped=x/(1.+x);
        else mapped=clamp(x,0.,1.);
    }
    if(effects.y>.5){float l=dot(mapped,vec3(.2126,.7152,.0722));mapped=mix(vec3(l),mapped,grading.x);mapped=(mapped-.18)*grading.y+.18;}
    color=vec4(pow(clamp(mapped,0.,1.),vec3(1/exposureGamma.y)),1);
}
