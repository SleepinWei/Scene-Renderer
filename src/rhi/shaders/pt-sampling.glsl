// Matches PT/Sampler.cpp; padded 2D Sobol with independently hashed digital shifts.
uint ptHash(uint x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float sobol(uint pixelSeed,uint sampleIndex,uint dimension) {
    uint gray=sampleIndex^(sampleIndex>>1),direction=0x80000000u,value=0u;
    while(gray!=0u){if((gray&1u)!=0u)value^=direction;direction=(dimension&1u)!=0u?direction^(direction>>1):direction>>1;gray>>=1;}
    value^=ptHash(pixelSeed^ptHash(dimension/2u+0x9e3779b9u)^((dimension&1u)*0xa511e9b3u));
    return float(value>>8)*(1.0/16777216.0);
}
