#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std140) uniform BloomParameters {vec4 options;}; // threshold,knee,first,scatter; first=-1 for upsample
layout(set=0,binding=1) uniform sampler2D sourceColor;
layout(set=0,binding=2) uniform sampler2D lowColor;
vec3 bright(vec3 x) {
    x=max(x,vec3(0));
    if(options.z<.5)return x;
    float peak=max(x.r,max(x.g,x.b)),k=max(options.y,1e-5);
    float soft=clamp(peak-options.x+k,0.,2.*k);soft=soft*soft/(4.*k);
    return x*max(soft,peak-options.x)/max(peak,1e-5);
}
void main(){
    vec2 pixel=1./vec2(options.z<0.?textureSize(lowColor,0):textureSize(sourceColor,0));
    vec3 sum=vec3(0);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        float weight=float((x==0?2:1)*(y==0?2:1))/16.;
        vec2 p=clamp(uv+vec2(x,y)*pixel,vec2(0),vec2(1));
        sum+=weight*(options.z<0.?texture(lowColor,p).rgb:bright(texture(sourceColor,p).rgb));
    }
    if(options.z<0.)sum=mix(texture(sourceColor,uv).rgb,sum,options.w);
    color=vec4(min(max(sum,vec3(0)),vec3(65000)),1);
}
