#include "PT/Denoiser.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>
#ifdef SCENERENDERER_HAS_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

namespace pt {
namespace {
bool finite(glm::vec3 c) {return std::isfinite(c.x)&&std::isfinite(c.y)&&std::isfinite(c.z);}
void validate(const std::vector<glm::vec3> &pixels,size_t count,const char *name,bool positive) {
    if(pixels.size()!=count)throw std::invalid_argument(std::string("OIDN: wrong ")+name+" image size");
    for(auto c:pixels)if(!finite(c)||(positive&&glm::any(glm::lessThan(c,glm::vec3(0)))))throw std::invalid_argument(std::string("OIDN: invalid ")+name+" pixel");
}
}
bool denoiserAvailable() {
#ifdef SCENERENDERER_HAS_OIDN
    return true;
#else
    return false;
#endif
}
void denoise(Image &image,const DenoiseOptions &options) {
    if(options.device!="auto"&&options.device!="cpu"&&options.device!="metal")throw std::invalid_argument("OIDN: device must be auto, cpu or metal");
    const size_t count=size_t(image.width)*image.height;
    if(!image.width||!image.height||image.width>16384||image.height>16384)throw std::invalid_argument("OIDN: invalid dimensions");
    validate(image.radiance,count,"color",true);
    const bool auxiliary=options.auxiliary&&!image.albedo.empty()&&!image.normal.empty();
    if(auxiliary){validate(image.albedo,count,"albedo",true);validate(image.normal,count,"normal",false);}
#ifndef SCENERENDERER_HAS_OIDN
    throw std::runtime_error("Open Image Denoise was not built: configure SCENERENDERER_OIDN=ON and OpenImageDenoise_DIR (see docs/path-tracing-denoising.md)");
#else
    const auto start=std::chrono::steady_clock::now();
    auto device=oidn::newDevice(options.device=="cpu"?oidn::DeviceType::CPU:options.device=="metal"?oidn::DeviceType::Metal:oidn::DeviceType::Default);
    auto check=[&](){const char *message=nullptr;const auto error=device?device.getError(message):oidn::getError(message);if(error!=oidn::Error::None)throw std::runtime_error(std::string("OIDN: ")+(message?message:"unknown device error"));};
    check();if(!device)throw std::runtime_error("OIDN: could not create device");
    device.commit();check();
    // Never assume glm::vec3 is packed or host memory is directly GPU-accessible.
    const size_t bytes=count*3*sizeof(float);
    auto upload=[&](const std::vector<glm::vec3> &pixels,bool albedo=false,bool normal=false){
        std::vector<float> packed(count*3);for(size_t i=0;i<count;++i){auto c=pixels[i];if(albedo)c=glm::clamp(c,0.f,1.f);if(normal)c=glm::clamp(c,-1.f,1.f);for(int k=0;k<3;++k)packed[i*3+k]=c[k];}
        auto buffer=device.newBuffer(bytes);check();if(!buffer)throw std::runtime_error("OIDN: could not allocate image buffer");buffer.write(0,bytes,packed.data());check();return buffer;
    };
    auto color=upload(image.radiance),output=device.newBuffer(bytes);check();
    oidn::BufferRef albedo,normal;
    if(auxiliary){
        albedo=upload(image.albedo,true);normal=upload(image.normal,false,true);
        // Prefilter private copies, preserving the original renderer AOVs.
        for(auto feature:{std::pair<const char*,oidn::BufferRef>{"albedo",albedo},{"normal",normal}}){
            auto filter=device.newFilter("RT");check();filter.setImage(feature.first,feature.second,oidn::Format::Float3,image.width,image.height);
            filter.setImage("output",feature.second,oidn::Format::Float3,image.width,image.height);filter.set("quality",oidn::Quality::High);
            filter.commit();check();filter.execute();check();
        }
    }
    auto filter=device.newFilter("RT");check();
    filter.setImage("color",color,oidn::Format::Float3,image.width,image.height);
    filter.setImage("output",output,oidn::Format::Float3,image.width,image.height);
    if(auxiliary){filter.setImage("albedo",albedo,oidn::Format::Float3,image.width,image.height);filter.setImage("normal",normal,oidn::Format::Float3,image.width,image.height);filter.set("cleanAux",true);}
    filter.set("hdr",true);filter.set("srgb",false);filter.set("quality",oidn::Quality::High);
    filter.commit();check();filter.execute();check();
    std::vector<float> packed(count*3);output.read(0,bytes,packed.data());check();
    std::vector<glm::vec3> result(count);for(size_t i=0;i<count;++i)result[i]={packed[i*3],packed[i*3+1],packed[i*3+2]};validate(result,count,"output",true);
    std::string backend;switch(device.get<oidn::DeviceType>("type")){case oidn::DeviceType::CPU:backend="CPU";break;case oidn::DeviceType::Metal:backend="Metal";break;case oidn::DeviceType::CUDA:backend="CUDA";break;case oidn::DeviceType::HIP:backend="HIP";break;case oidn::DeviceType::SYCL:backend="SYCL";break;default:backend="unknown";}
    auto version=std::to_string(device.get<int>("versionMajor"))+"."+std::to_string(device.get<int>("versionMinor"))+"."+std::to_string(device.get<int>("versionPatch"));check();
    image.denoised=std::move(result);image.denoiser="Open Image Denoise "+version;image.denoiseDevice=backend;image.denoiseAuxiliary=auxiliary;
    image.denoiseSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<image.denoiser<<" ("<<backend<<", "<<(auxiliary?"prefiltered albedo + normal":"color only")<<"): "<<image.denoiseSeconds<<" s\n";
#endif
}
Image readPfm(const std::string &path) {
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("PT: cannot read "+path);
    auto line=[&](){std::string value;while(std::getline(file,value)){if(!value.empty()&&value.back()=='\r')value.pop_back();if(!value.empty()&&value[0]!='#')return value;}throw std::invalid_argument("PT: truncated PFM header");};
    if(line()!="PF")throw std::invalid_argument("PT: expected RGB Float3 PFM");
    Image image;std::string extra;std::istringstream dimensions(line());if(!(dimensions>>image.width>>image.height)||dimensions>>extra||!image.width||!image.height||image.width>16384||image.height>16384)throw std::invalid_argument("PT: invalid PFM dimensions");
    float scale;std::istringstream scaling(line());if(!(scaling>>scale)||scaling>>extra||!std::isfinite(scale)||scale==0)throw std::invalid_argument("PT: invalid PFM scale");
    const size_t count=size_t(image.width)*image.height;std::vector<float> packed(count*3);file.read(reinterpret_cast<char*>(packed.data()),std::streamsize(packed.size()*sizeof(float)));if(!file)throw std::invalid_argument("PT: truncated PFM pixels");
    const uint16_t endian=1;const bool swap=(*reinterpret_cast<const uint8_t*>(&endian)==1)!=(scale<0);
    if(swap)for(auto &value:packed){auto *p=reinterpret_cast<uint8_t*>(&value);std::swap(p[0],p[3]);std::swap(p[1],p[2]);}
    image.radiance.resize(count);for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x){const size_t src=(size_t(image.height-1-y)*image.width+x)*3;image.radiance[size_t(y)*image.width+x]=std::abs(scale)*glm::vec3(packed[src],packed[src+1],packed[src+2]);}
    // Normal AOV PFM may contain negative components; beauty is checked by denoise().
    validate(image.radiance,count,"PFM",false);return image;
}
}
