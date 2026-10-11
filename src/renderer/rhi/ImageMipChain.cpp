#include "renderer/rhi/ImageMipChain.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace render {
ImageMipPolicy materialMipPolicy(size_t slot,const MaterialParameters& p){
    if(slot==0)return {ImageMipFilter::Color,p.albedoAlpha.w>0?std::clamp(p.factors.w/p.albedoAlpha.w,0.f,1.f):0};
    return {slot==3?ImageMipFilter::Roughness:ImageMipFilter::Linear,0};
}
std::vector<ImageRGBA8> buildImageMipChain(const ImageRGBA8& base,ImageMipPolicy policy){
    std::vector<ImageRGBA8> result;
    if(policy.filter==ImageMipFilter::None)return result;
    static const auto decoded=[](){std::array<float,256> a{};for(size_t i=0;i<a.size();++i)a[i]=std::pow(float(i)/255,2.2f);return a;}();
    auto byte=[](float v){return uint8_t(std::clamp(std::lround(v*255),0l,255l));};
    const auto* source=&base;
    float coverage=0;
    if(policy.alphaCutoff>0){for(size_t i=3;i<base.pixels.size();i+=4)coverage+=base.pixels[i]/255.f>=policy.alphaCutoff;coverage/=float(base.width)*base.height;}
    while(source->width>1||source->height>1){
        ImageRGBA8 mip;mip.width=std::max(1u,source->width/2);mip.height=std::max(1u,source->height/2);mip.pixels.resize(size_t(mip.width)*mip.height*4);
        // Area weights include the last row/column of non-power-of-two images.
        for(uint32_t y=0;y<mip.height;++y)for(uint32_t x=0;x<mip.width;++x){
            const float x0=float(x)*source->width/mip.width,x1=float(x+1)*source->width/mip.width;
            const float y0=float(y)*source->height/mip.height,y1=float(y+1)*source->height/mip.height;
            std::array<float,4> sum{};float area=0;
            for(uint32_t sy=uint32_t(y0);sy<uint32_t(std::ceil(y1));++sy)for(uint32_t sx=uint32_t(x0);sx<uint32_t(std::ceil(x1));++sx){
                const float weight=(std::min(x1,float(sx+1))-std::max(x0,float(sx)))*(std::min(y1,float(sy+1))-std::max(y0,float(sy)));
                const auto* p=&source->pixels[(size_t(sy)*source->width+sx)*4];const float alpha=p[3]/255.f;area+=weight;
                for(size_t c=0;c<4;++c){float v=p[c]/255.f;if(policy.filter==ImageMipFilter::Color&&c<3)v=decoded[p[c]]*alpha;
                    if(policy.filter==ImageMipFilter::Roughness&&c==1)v=v*v*v*v;sum[c]+=v*weight;}
            }
            for(size_t c=0;c<4;++c){float v=sum[c]/area;
                if(policy.filter==ImageMipFilter::Color&&c<3)v=std::pow(sum[3]>1e-8f?sum[c]/sum[3]:0.f,1/2.2f);
                if(policy.filter==ImageMipFilter::Roughness&&c==1)v=std::pow(v,.25f);
                mip.pixels[(size_t(y)*mip.width+x)*4+c]=byte(v);
            }
        }
        if(policy.alphaCutoff>0&&coverage>0&&coverage<1){
            auto fraction=[&](float scale){size_t n=0;for(size_t i=3;i<mip.pixels.size();i+=4)n+=std::min(1.f,mip.pixels[i]/255.f*scale)>=policy.alphaCutoff;return float(n)/(mip.width*mip.height);};
            float lo=0,hi=8;for(int i=0;i<16;++i){float mid=(lo+hi)*.5f;if(fraction(mid)<coverage)lo=mid;else hi=mid;}
            const float scale=std::abs(fraction(lo)-coverage)<std::abs(fraction(hi)-coverage)?lo:hi;
            for(size_t i=3;i<mip.pixels.size();i+=4)mip.pixels[i]=byte(mip.pixels[i]/255.f*scale);
        }
        result.push_back(std::move(mip));source=&result.back();
    }
    return result;
}
}
