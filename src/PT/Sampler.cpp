#include "PT/Sampler.h"
namespace pt {
uint32_t sampleHash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16);
}
float sobolSample(uint32_t pixelSeed, uint32_t sample, uint32_t dimension) {
    const uint32_t pairSeed=sampleHash(pixelSeed^sampleHash(dimension/2+0x9e3779b9u));
    // Permute each dyadic index block independently for each padded pair.
    // Every power-of-two prefix retains its complete 2D net, while different
    // bounces no longer share a fixed XOR relation between their coordinates.
    if(sample>=2){
        uint32_t block=1,bits=0;while(block<=sample/2){block<<=1;++bits;}
        uint32_t rightBits=bits/2,leftBits=bits-rightBits;
        uint32_t left=(sample-block)>>rightBits,right=(sample-block)&((1u<<rightBits)-1);
        const uint32_t seed=sampleHash(pairSeed^sampleHash(block));
        for(uint32_t round=0;round<6;++round){
            const uint32_t next=left^(sampleHash(right^seed^sampleHash(round+0x9e3779b9u))&((1u<<leftBits)-1));
            left=right;right=next;const uint32_t swap=leftBits;leftBits=rightBits;rightBits=swap;
        }
        sample=block+(left<<rightBits|right);
    }
    uint32_t gray = sample ^ (sample >> 1), direction = 0x80000000u, value = 0;
    while (gray) {
        if (gray & 1u) value ^= direction;
        direction = (dimension & 1u) ? direction ^ (direction >> 1) : direction >> 1;
        gray >>= 1;
    }
    value ^= sampleHash(pixelSeed ^ sampleHash(dimension / 2 + 0x9e3779b9u) ^ ((dimension & 1u) * 0xa511e9b3u));
    return float(value >> 8) * (1.f / 16777216.f);
}
Random::Random(uint64_t seed):state(seed+0x9e3779b97f4a7c15ull) { bits(); }
Random Random::forPixel(uint64_t seed,uint32_t pixel,uint32_t sample,bool sobol) {
    Random r(seed ^ (uint64_t(pixel)*0xd1b54a32d192ed03ull) ^ (uint64_t(sample)*0x94d049bb133111ebull));
    r.sobol_=sobol;r.sample_=sample;r.pixelSeed_=sampleHash(uint32_t(seed)^sampleHash(uint32_t(seed>>32))^sampleHash(pixel));return r;
}
uint32_t Random::bits() {
    const uint64_t previous=state;state=previous*6364136223846793005ull+1442695040888963407ull;
    const uint32_t x=uint32_t(((previous>>18)^previous)>>27),rotation=uint32_t(previous>>59);
    return (x>>rotation)|(x<<((-rotation)&31));
}
float Random::uniform() {return sobol_?sobolSample(pixelSeed_,sample_,dimension_++):float(bits()>>8)*(1.f/16777216.f);}
std::array<float,2> Random::uniform2() {if(sobol_ && (dimension_&1u))++dimension_;return {uniform(),uniform()};}
} // namespace pt
