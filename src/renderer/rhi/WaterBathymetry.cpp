#include "renderer/rhi/OceanSurface.h"
#include "renderer/rhi/GpuVirtualTexture.h"
#include "engine/JobSystem.h"
#include "engine/AssetPath.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace render {
namespace {
struct Raster {uint32_t size;std::vector<glm::vec4> pixels;};
Raster sourceRaster(const VirtualTextureSource& source,uint32_t maxExtent=1024) {
    uint32_t mip=0,n=source.extent;while(n>maxExtent){n>>=1;++mip;}
    Raster out{n,std::vector<glm::vec4>(size_t(n)*n)};
    const bool floating=source.formats.at(0)==rhi::Format::RGBA32Float;
    if(!floating && source.formats.at(0)!=rhi::Format::RGBA8UNorm)throw std::invalid_argument("Bathymetry source format");
    std::lock_guard<std::mutex> lock(*source.ioMutex);
    for(uint32_t y=0;y<n/64;++y)for(uint32_t x=0;x<n/64;++x) {
        engine::CancellationScope::check();auto page=source.readPage(mip,x,y);
        if(page.empty()||page[0].size()!=size_t(68)*68*(floating?16:4))throw std::invalid_argument("Bathymetry page size");
        for(uint32_t yy=0;yy<64;++yy)for(uint32_t xx=0;xx<64;++xx) {
            size_t at=(size_t(yy+2)*68+xx+2)*4;glm::vec4 v;
            if(floating)std::memcpy(&v,page[0].data()+at*4,16);
            else for(int c=0;c<4;++c)v[c]=page[0][at+c]/255.f;
            if(!std::isfinite(v.x))throw std::invalid_argument("Nonfinite bathymetry");
            out.pixels[size_t(y*64+yy)*n+x*64+xx]=v;
        }
    }
    return out;
}
glm::vec4 sample(const Raster& r,float u,float v) {
    float x=std::clamp(u,0.f,1.f)*(r.size-1),y=std::clamp(v,0.f,1.f)*(r.size-1);
    auto a=uint32_t(x),b=uint32_t(y),aa=std::min(a+1,r.size-1),bb=std::min(b+1,r.size-1);
    return glm::mix(glm::mix(r.pixels[b*r.size+a],r.pixels[b*r.size+aa],x-a),glm::mix(r.pixels[bb*r.size+a],r.pixels[bb*r.size+aa],x-a),y-b);
}
}
std::shared_ptr<const WaterBathymetry> prepareWaterBathymetry(const VirtualTextureSource& height,const VirtualTextureSource& material,glm::vec3 albedoFactor) {
    for(int c=0;c<3;++c)if(!std::isfinite(albedoFactor[c])||albedoFactor[c]<0)throw std::invalid_argument("Invalid bathymetry albedo factor");
    auto h=sourceRaster(height),m=sourceRaster(material,2048);auto out=std::make_shared<WaterBathymetry>();out->size=std::max(h.size,m.size);
    out->heightColor.resize(size_t(out->size)*out->size);
    for(uint32_t y=0;y<out->size;++y)for(uint32_t x=0;x<out->size;++x) {
        // Terrain vertices invert V for material UVs, but not for height UVs.
        auto color=sample(m,float(x)/(out->size-1),1.f-float(y)/(out->size-1));
        out->heightColor[size_t(y)*out->size+x]={sample(h,float(x)/(out->size-1),float(y)/(out->size-1)).x,color.r*albedoFactor.r,color.g*albedoFactor.g,color.b*albedoFactor.b};
    }
    return out;
}
}
