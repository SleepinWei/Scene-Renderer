// Matches PT/Sampler.cpp; independently permuted dyadic blocks preserve 2D nets.
uint ptHash(uint x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float sobol(uint pixelSeed,uint sampleIndex,uint dimension) {
    uint pairSeed=ptHash(pixelSeed^ptHash(dimension/2u+0x9e3779b9u));
    if(sampleIndex>=2u){
        uint block=1u,bits=0u;while(block<=sampleIndex/2u){block<<=1u;++bits;}
        uint rightBits=bits/2u,leftBits=bits-rightBits;
        uint left=(sampleIndex-block)>>rightBits,right=(sampleIndex-block)&((1u<<rightBits)-1u);
        uint seed=ptHash(pairSeed^ptHash(block));
        for(uint round=0u;round<6u;++round){
            uint next=left^(ptHash(right^seed^ptHash(round+0x9e3779b9u))&((1u<<leftBits)-1u));
            left=right;right=next;uint swap=leftBits;leftBits=rightBits;rightBits=swap;
        }
        sampleIndex=block+((left<<rightBits)|right);
    }
    uint gray=sampleIndex^(sampleIndex>>1),direction=0x80000000u,value=0u;
    while(gray!=0u){if((gray&1u)!=0u)value^=direction;direction=(dimension&1u)!=0u?direction^(direction>>1):direction>>1;gray>>=1;}
    value^=ptHash(pixelSeed^ptHash(dimension/2u+0x9e3779b9u)^((dimension&1u)*0xa511e9b3u));
    return float(value>>8)*(1.0/16777216.0);
}
