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
#include <thread>
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
struct DenoiserContext::Impl {
    std::thread::id owner=std::this_thread::get_id();
    DenoiserStats stats;
#ifdef SCENERENDERER_HAS_OIDN
    oidn::DeviceRef device;
    oidn::BufferRef color,output,albedo,normal;
    oidn::FilterRef beauty,albedoFilter,normalFilter;
    std::vector<float> packed;
    std::string deviceName;
    uint32_t width=0,height=0;
    bool auxiliary=false;
    void check() {
        const char *message=nullptr;const auto error=device?device.getError(message):oidn::getError(message);
        if(error!=oidn::Error::None)throw std::runtime_error(std::string("OIDN: ")+(message?message:"unknown device error"));
    }
    void clearFilters() {
        beauty.release();albedoFilter.release();normalFilter.release();
        color.release();output.release();albedo.release();normal.release();
        width=height=0;stats.bufferBytes=0;std::vector<float>().swap(packed);
    }
    void prepare(uint32_t w,uint32_t h,bool aux,const std::string &name) {
        if(!device||deviceName!=name){
            clearFilters();device.release();deviceName.clear();
            device=oidn::newDevice(name=="cpu"?oidn::DeviceType::CPU:name=="metal"?oidn::DeviceType::Metal:oidn::DeviceType::Default);
            check();if(!device)throw std::runtime_error("OIDN: could not create device");device.commit();check();deviceName=name;++stats.deviceBuilds;
        }
        if(beauty&&width==w&&height==h&&auxiliary==aux)return;
        clearFilters();const size_t bytes=size_t(w)*h*3*sizeof(float);
        auto buffer=[&]{auto b=device.newBuffer(bytes);check();if(!b)throw std::runtime_error("OIDN: could not allocate image buffer");return b;};
        color=buffer();output=buffer();
        if(aux){
            albedo=buffer();normal=buffer();
            auto feature=[&](const char *name,const oidn::BufferRef &b){auto f=device.newFilter("RT");check();f.setImage(name,b,oidn::Format::Float3,w,h);f.setImage("output",b,oidn::Format::Float3,w,h);f.set("quality",oidn::Quality::High);f.commit();check();return f;};
            albedoFilter=feature("albedo",albedo);normalFilter=feature("normal",normal);
        }
        auto f=device.newFilter("RT");check();
        f.setImage("color",color,oidn::Format::Float3,w,h);f.setImage("output",output,oidn::Format::Float3,w,h);
        if(aux){f.setImage("albedo",albedo,oidn::Format::Float3,w,h);f.setImage("normal",normal,oidn::Format::Float3,w,h);f.set("cleanAux",true);}
        f.set("hdr",true);f.set("srgb",false);f.set("quality",oidn::Quality::High);f.commit();check();beauty=std::move(f);
        width=w;height=h;auxiliary=aux;packed.resize(size_t(w)*h*3);stats.bufferBytes=bytes*(aux?4:2);stats.filterBuilds+=aux?3:1;
    }
    void upload(const std::vector<glm::vec3> &pixels,oidn::BufferRef &buffer,int feature=0) {
        for(size_t i=0;i<pixels.size();++i){auto c=pixels[i];if(feature==1)c=glm::clamp(c,0.f,1.f);if(feature==2)c=glm::clamp(c,-1.f,1.f);for(int k=0;k<3;++k)packed[i*3+k]=c[k];}
        buffer.write(0,packed.size()*sizeof(float),packed.data());check();
    }
#endif
};
DenoiserContext::DenoiserContext():impl_(std::make_unique<Impl>()){}
DenoiserContext::~DenoiserContext()=default;
const DenoiserStats &DenoiserContext::statistics()const{return impl_->stats;}
void DenoiserContext::execute(Image &image,const DenoiseOptions &options) {
    auto &s=*impl_;if(s.owner!=std::this_thread::get_id())throw std::logic_error("OIDN context belongs to another thread");
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
    s.prepare(image.width,image.height,auxiliary,options.device);s.upload(image.radiance,s.color);
    if(auxiliary){s.upload(image.albedo,s.albedo,1);s.upload(image.normal,s.normal,2);s.albedoFilter.execute();s.check();s.normalFilter.execute();s.check();}
    s.beauty.execute();s.check();s.output.read(0,s.packed.size()*sizeof(float),s.packed.data());s.check();
    std::vector<glm::vec3> result(count);for(size_t i=0;i<count;++i)result[i]={s.packed[i*3],s.packed[i*3+1],s.packed[i*3+2]};validate(result,count,"output",true);
    std::string backend;switch(s.device.get<oidn::DeviceType>("type")){case oidn::DeviceType::CPU:backend="CPU";break;case oidn::DeviceType::Metal:backend="Metal";break;case oidn::DeviceType::CUDA:backend="CUDA";break;case oidn::DeviceType::HIP:backend="HIP";break;case oidn::DeviceType::SYCL:backend="SYCL";break;default:backend="unknown";}
    auto version=std::to_string(s.device.get<int>("versionMajor"))+"."+std::to_string(s.device.get<int>("versionMinor"))+"."+std::to_string(s.device.get<int>("versionPatch"));s.check();
    image.denoised=std::move(result);image.denoiser="Open Image Denoise "+version;image.denoiseDevice=backend;image.denoiseAuxiliary=auxiliary;
    image.denoiseSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();++s.stats.executions;
    std::cout<<image.denoiser<<" ("<<backend<<", "<<(auxiliary?"prefiltered albedo + normal":"color only")<<"): "<<image.denoiseSeconds<<" s\n";
#endif
}
void denoise(Image &image,const DenoiseOptions &options) {DenoiserContext context;context.execute(image,options);}
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
