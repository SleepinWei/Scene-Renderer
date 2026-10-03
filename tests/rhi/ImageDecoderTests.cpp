#include "renderer/rhi/GpuMaterial.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace render {ImageRGBA8 decodeDds(const std::string&);}
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void word(std::vector<uint8_t>& b,size_t at,uint32_t v){for(int i=0;i<4;++i)b.at(at+i)=uint8_t(v>>(i*8));}
std::vector<uint8_t> header(uint32_t fourcc,uint32_t width=4,uint32_t height=4,size_t data=8){std::vector<uint8_t> b(128+data);word(b,0,0x20534444);word(b,4,124);word(b,12,height);word(b,16,width);word(b,76,32);word(b,80,4);word(b,84,fourcc);return b;}
void redBlock(std::vector<uint8_t>& b,size_t at){b.at(at)=0;b.at(at+1)=0xf8;}
}
int main(){
    auto path=std::filesystem::temp_directory_path()/("rhi-dds-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".dds");
    auto decode=[&](const std::vector<uint8_t>& b){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();return render::decodeDds(path.string());};
    auto rejects=[&](const std::vector<uint8_t>& b){bool rejected=false;try{decode(b);}catch(const std::runtime_error&){rejected=true;}check(rejected,"Malformed DDS accepted");};
    try {
        auto b=header(0x31545844);redBlock(b,128);auto image=decode(b);check(image.width==4&&image.height==4,"DDS dimensions");for(size_t i=0;i<image.pixels.size();i+=4)check(image.pixels[i]==255&&image.pixels[i+1]==0&&image.pixels[i+2]==0&&image.pixels[i+3]==255,"BC1 red decode");
        b=header(0x31545844);b[129]=0x20;image=decode(b);check(image.pixels[0]==33,"BC1 RGB565 bit expansion");
        b=header(0x31545844);word(b,132,0xffffffff);image=decode(b);check(image.pixels[3]==0,"BC1 transparent palette");
        b=header(0x33545844,4,4,16);for(size_t i=128;i<136;++i)b[i]=0x55;redBlock(b,136);image=decode(b);check(image.pixels[0]==255&&image.pixels[3]==85,"BC2 explicit alpha");
        b=header(0x35545844,4,4,16);b[128]=255;b[129]=0;b[130]=2;redBlock(b,136);image=decode(b);check(image.pixels[3]==218,"BC3 seven-step alpha interpolation");
        b[128]=0;b[129]=128;b[130]=6;image=decode(b);check(image.pixels[3]==0,"BC3 fixed zero alpha");b[130]=7;image=decode(b);check(image.pixels[3]==255,"BC3 fixed opaque alpha");
        b=header(0x31545844,5,3,16);redBlock(b,128);redBlock(b,136);image=decode(b);check(image.pixels.size()==5*3*4&&image.pixels.back()==255,"BC1 partial edge blocks");
        b=header(0x30315844,4,4,28);word(b,128,71);word(b,132,3);word(b,140,1);redBlock(b,148);image=decode(b);check(image.pixels[0]==255,"DX10 BC1 header");word(b,140,2);rejects(b);
        b=header(0,1,1,4);word(b,80,0x41);word(b,88,32);word(b,92,0xff0000);word(b,96,0xff00);word(b,100,0xff);word(b,104,0xff000000);word(b,128,0x80402010);image=decode(b);check(image.pixels==std::vector<uint8_t>({64,32,16,128}),"RGBA channel masks");
        b=header(0x31545844);b.resize(135);rejects(b);b=header(0x31545844);word(b,112,0x200);rejects(b);word(b,112,0);word(b,16,0);rejects(b);rejects({});
        std::filesystem::remove(path);std::cout<<"DDS BC1/BC2/BC3, DX10, partial blocks, RGBA masks and malformed input passed\n";return 0;
    }catch(const std::exception& e){std::filesystem::remove(path);std::cerr<<e.what()<<'\n';return 1;}
}
