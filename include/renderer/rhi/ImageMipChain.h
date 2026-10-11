#pragma once
#include "renderer/rhi/GpuMaterial.h"
namespace render {
enum class ImageMipFilter { None, Linear, Color, Roughness };
struct ImageMipPolicy {
    ImageMipFilter filter=ImageMipFilter::None;
    float alphaCutoff=0;
    bool operator==(const ImageMipPolicy& b)const{return filter==b.filter&&alphaCutoff==b.alphaCutoff;}
};
// Color follows the renderer's pow(2.2) decode; normal/data remain linear.
// Roughness preserves the second GGX slope moment (r^4). Normal mips retain
// mean-vector length rather than inventing full-strength detail after reduction.
std::vector<ImageRGBA8> buildImageMipChain(const ImageRGBA8&,ImageMipPolicy);
ImageMipPolicy materialMipPolicy(size_t slot,const MaterialParameters&);
}
