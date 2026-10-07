#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std140) uniform PostFinishParameters {
    vec4 options; // fxaa,sharpness,chromatic pixels,grain
    vec4 vignette; // strength,roundness,time,unused
};
layout(set=0,binding=1) uniform sampler2D sourceColor;
float luminance(vec3 c){return dot(c,vec3(.299,.587,.114));}
vec3 sampleLens(vec2 p){
    vec3 c=texture(sourceColor,p).rgb;
    if(options.z>0.){
        vec2 offset=(p-.5)*options.z*2./vec2(textureSize(sourceColor,0));
        c.r=texture(sourceColor,clamp(p+offset,vec2(0),vec2(1))).r;
        c.b=texture(sourceColor,clamp(p-offset,vec2(0),vec2(1))).b;
    }
    return c;
}
vec3 aa(vec2 p){
    vec2 pixel=1./vec2(textureSize(sourceColor,0));
    vec3 center=sampleLens(p);
    if(options.x<.5)return center;
    float nw=luminance(sampleLens(p+vec2(-1,-1)*pixel)),ne=luminance(sampleLens(p+vec2(1,-1)*pixel));
    float sw=luminance(sampleLens(p+vec2(-1,1)*pixel)),se=luminance(sampleLens(p+pixel)),m=luminance(center);
    float lo=min(m,min(min(nw,ne),min(sw,se))),hi=max(m,max(max(nw,ne),max(sw,se)));
    if(hi-lo<max(.0312,hi*.125))return center;
    vec2 direction=vec2(-((nw+ne)-(sw+se)),(nw+sw)-(ne+se));
    float reduce=max((nw+ne+sw+se)*(.25/8.),1./128.);
    direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),vec2(-8),vec2(8))*pixel;
    vec3 a=.5*(sampleLens(p+direction*(-1./6.))+sampleLens(p+direction*(1./6.)));
    vec3 b=a*.5+.25*(sampleLens(p-direction*.5)+sampleLens(p+direction*.5));
    float l=luminance(b);return l<lo||l>hi?a:b;
}
float noise(vec2 p){vec3 q=fract(vec3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);return fract((q.x+q.y)*q.z);}
void main(){
    vec2 pixel=1./vec2(textureSize(sourceColor,0));vec3 c=aa(uv);
    if(options.y>0.){
        vec3 n=aa(uv+vec2(pixel.x,0)),s=aa(uv-vec2(pixel.x,0)),e=aa(uv+vec2(0,pixel.y)),w=aa(uv-vec2(0,pixel.y));
        vec3 low=min(c,min(min(n,s),min(e,w))),high=max(c,max(max(n,s),max(e,w)));
        c=clamp(c+(c-(n+s+e+w)*.25)*options.y,low,high);
    }
    float radial=length((uv-.5)*vec2(1, mix(.5,1.,vignette.y)))*1.41421356;
    c*=1.-vignette.x*smoothstep(.25,1.,radial);
    if(options.w>0.)c+=(noise(floor(uv/pixel)+floor(vignette.z*24.)*vec2(19.1,7.7))-.5)*options.w;
    color=vec4(clamp(c,0.,1.),1);
}
