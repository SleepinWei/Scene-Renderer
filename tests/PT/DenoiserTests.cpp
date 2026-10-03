#include "PT/Denoiser.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void pfm() {
    const auto path=std::filesystem::temp_directory_path()/"scene-renderer-denoiser-test.pfm";
    // Big-endian, scale 2, two rows. Also exercises signed normal components.
    {std::ofstream file(path,std::ios::binary);file<<"PF\n# test\n1 2\n2.0\n";for(float value:{.5f,.25f,-.5f,1.f,2.f,3.f}){unsigned char bytes[4];std::memcpy(bytes,&value,4);const uint16_t endian=1;if(*reinterpret_cast<const unsigned char*>(&endian)){std::swap(bytes[0],bytes[3]);std::swap(bytes[1],bytes[2]);}file.write(reinterpret_cast<char*>(bytes),4);}}
    auto image=pt::readPfm(path.string());check(image.width==1&&image.height==2,"PFM dimensions");check(image.radiance[0]==glm::vec3(2,4,6)&&image.radiance[1]==glm::vec3(1,.5f,-1),"PFM endian, scale or row order");
    {std::ofstream file(path,std::ios::binary);file<<"PF\n1 2\n-1.0\n";}bool rejected=false;try{pt::readPfm(path.string());}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Truncated PFM accepted");std::filesystem::remove(path);
}
void filter() {
    pt::Image image;image.width=128;image.height=96;const size_t count=size_t(image.width)*image.height;
    std::vector<glm::vec3> reference(count);image.radiance.resize(count);image.albedo.resize(count);image.normal.resize(count);
    std::mt19937 random(37);std::normal_distribution<float> noise(0,.22f);
    for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x){const size_t i=size_t(y)*image.width+x;reference[i]=x<64?glm::vec3(.4f,.6f,.8f):glm::vec3(2,3,4);image.albedo[i]=x<64?glm::vec3(.2f,.3f,.4f):glm::vec3(.5f,.75f,1);image.normal[i]=x<64?glm::vec3(0,0,1):glm::vec3(0,1,0);image.radiance[i]=glm::max(reference[i]+glm::vec3(noise(random),noise(random),noise(random)),glm::vec3(0));}
    const auto raw=image.radiance,albedo=image.albedo,normal=image.normal;
    if(!pt::denoiserAvailable()){bool rejected=false;try{pt::denoise(image,{"cpu",true});}catch(const std::runtime_error &){rejected=true;}check(rejected,"Unavailable denoiser silently succeeded");return;}
    pt::denoise(image,{"cpu",true});
    check(image.radiance==raw&&image.albedo==albedo&&image.normal==normal,"Denoiser mutated original HDR or AOVs");check(image.denoised.size()==count&&image.denoiseAuxiliary&&image.denoiseDevice=="CPU","Missing denoise output or metadata");
    double before=0,after=0;glm::vec3 left(0),right(0);size_t n=0;
    for(uint32_t y=8;y<88;++y)for(uint32_t x=8;x<120;++x){if(x>=56&&x<72)continue;size_t i=size_t(y)*128+x;auto c=image.denoised[i];check(std::isfinite(c.x)&&std::isfinite(c.y)&&std::isfinite(c.z),"Non-finite denoised pixel");before+=glm::dot(raw[i]-reference[i],raw[i]-reference[i]);after+=glm::dot(c-reference[i],c-reference[i]);if(x<56)left+=c;else right+=c;++n;}
    left/=float(n/2);right/=float(n/2);std::cout<<"OIDN MSE ratio "<<after/before<<", left "<<left.x<<", HDR right "<<right.z<<'\n';check(after<before*.4,"OIDN did not reduce noise");check(glm::length(left-glm::vec3(.4f,.6f,.8f))<.1f&&glm::length(right-glm::vec3(2,3,4))<.15f,"OIDN lost edge-separated HDR energy");
    pt::denoise(image,{"cpu",false});check(!image.denoiseAuxiliary&&image.denoised.size()==count,"Color-only denoising failed");
    image.radiance[0].x=std::numeric_limits<float>::quiet_NaN();bool rejected=false;try{pt::denoise(image,{"cpu",false});}catch(const std::invalid_argument &){rejected=true;}check(rejected,"NaN input accepted");
    image.radiance=raw;image.albedo.pop_back();rejected=false;try{pt::denoise(image,{"cpu",true});}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Truncated auxiliary input accepted");
}
}
int main(){try{pfm();filter();return 0;}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
