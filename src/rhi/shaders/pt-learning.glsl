// Frozen proposal/cache and separate integer-atomic training data. No float atomics.
struct GuideCell {uvec4 meta;vec4 tail;float cdf[64];float environmentCdf[64];};
layout(set=1,binding=5,std430) readonly buffer Guide {GuideCell guide[];};
layout(set=1,binding=6,std430) buffer Training {uint training[];};
const uint TRAIN_STRIDE=133u,TRAIN_LIMIT=8192u;
uint guideEvents,cacheEvents,activeGuide;
float guideFraction,environmentFraction;
uint cellKey(Surface s){
    ivec3 cell=ivec3(floor(s.position/p.guideSettings.x));vec3 a=abs(s.n);uint axis=a.x>a.y&&a.x>a.z?0u:a.y>a.z?1u:2u;
    uint normal=axis*2u+(s.n[axis]<0?1u:0u);
    return max(1u,ptHash(ptHash(uint(cell.x))^ptHash(uint(cell.y)+0x9e3779b9u)^ptHash(uint(cell.z)+0x85ebca6bu)^ptHash(normal+0xc2b2ae35u)));
}
uint guideSlot(Surface s){if(p.learning.x==0u)return 0xffffffffu;uint key=cellKey(s),slot=ptHash(key)&(p.learning.x-1u);return guide[slot].meta.x==key&&guide[slot].meta.y>=16u?slot:0xffffffffu;}
uint directionBin(vec3 d){float phi=fract(atan(d.z,d.x)/(2*PI));return min(7u,uint((d.y*.5+.5)*8))*8u+min(7u,uint(phi*8));}
float learnedPdf(vec3 d,bool env){if(activeGuide==0xffffffffu)return 0;uint bin=directionBin(d);float high=env?guide[activeGuide].environmentCdf[bin]:guide[activeGuide].cdf[bin],low=bin>0u?(env?guide[activeGuide].environmentCdf[bin-1u]:guide[activeGuide].cdf[bin-1u]):0;return (high-low)*64/(4*PI);}
float continuationPdf(Surface s,vec3 view,vec3 d){return mix(bsdfPdf(s,view,d),learnedPdf(d,false),guideFraction);}
vec3 learnedDirection(bool env){
    float selected=randomValue();uint bin=0u;while(bin<63u&&(env?guide[activeGuide].environmentCdf[bin]:guide[activeGuide].cdf[bin])<=selected)++bin;
    vec2 uv=randomPair();float phi=(float(bin%8u)+uv.x)*2*PI/8,cosine=(float(bin/8u)+uv.y)*2/8-1,sine=sqrt(max(0,1-cosine*cosine));return vec3(sine*cos(phi),cosine,sine*sin(phi));
}
vec3 guidedDirection(Surface s,vec3 view){if(guideFraction==0||randomValue()>=guideFraction)return sampleBsdf(s,view);return learnedDirection(false);}
float environmentProposalPdf(vec3 d){return mix(environmentPdf(d),learnedPdf(d,true),environmentFraction);}
vec3 guidedEnvironment(out float pdf){vec3 d;if(environmentFraction>0&&randomValue()<environmentFraction)d=learnedDirection(true);else d=sampleEnvironment(pdf);pdf=environmentProposalPdf(d);return d;}
void trainCell(Surface s,vec3 d,vec3 tail,vec3 envDirection,float envWeight){
    uint key=cellKey(s),slot=ptHash(key)&(p.learning.x-1u),base=slot*TRAIN_STRIDE;
    uint owner=atomicCompSwap(training[base],0u,key);if(owner!=0u&&owner!=key)return;
    uint count=atomicAdd(training[base+1u],1u);if(count>=TRAIN_LIMIT)return;
    vec3 normalized=tail/max(s.albedo,vec3(.01));
    for(uint c=0u;c<3u;++c)atomicAdd(training[base+2u+c],uint(clamp(normalized[c]*4096,0,65535)));
    // E[f*cos*Li/pdf | bin] learns a diffuse product proposal. Clipping only
    // changes the learned proposal; it never clips a final guided contribution.
    float weight=luminance(tail/max(s.albedo,vec3(.01)));
    if(envWeight>0&&!isnan(envWeight)&&!isinf(envWeight))atomicAdd(training[base+69u+directionBin(envDirection)],uint(clamp(envWeight*4096,0,262143)));
    if(weight>0&&!isnan(weight)&&!isinf(weight))atomicAdd(training[base+5u+directionBin(d)],uint(clamp(weight*256,0,262143)));
}
