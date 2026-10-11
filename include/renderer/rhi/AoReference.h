#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
namespace pt {class CpuScene;}
namespace render {
struct AoReferenceOptions {
    uint32_t width=320,height=240,samples=256,threads=4;
    uint64_t seed=1;
    float radius=1,bias=.025f,power=1.5f;
};
struct AoReferenceImage {
    std::vector<float> linear,visibility;
    std::vector<uint8_t> matched;
    uint64_t rays=0,saturatedRays=0;
    size_t covered=0,matchedPixels=0;
    double seconds=0;
};
// Trace cosine-weighted, finite-radius visibility from the rasterized world
// positions. Exact triangle normals are used; unmatched raster/CPU surfaces
// are excluded explicitly, rather than contaminating the comparison.
AoReferenceImage traceAoReference(const pt::CpuScene&,const FrameData&,
                                 const std::vector<float>& positions,const AoReferenceOptions&);
void validateAoReference();
int runAoBenchmark(int argc,char** argv);
}
