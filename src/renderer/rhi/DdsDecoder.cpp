#include "renderer/rhi/GpuMaterial.h"
#include <fstream>
#include <array>
#include <cstring>
#include <stdexcept>
namespace render {
ImageRGBA8 decodeDds(const std::string& path){
    std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
    auto word=[&](size_t at){if(at+4>bytes.size())throw std::runtime_error("Truncated DDS: "+path);return uint32_t(bytes[at])|uint32_t(bytes[at+1])<<8|uint32_t(bytes[at+2])<<16|uint32_t(bytes[at+3])<<24;};
    if(word(0)!=0x20534444 || word(4)!=124 || word(76)!=32)throw std::runtime_error("Invalid DDS header: "+path);
    const uint32_t width=word(16),height=word(12);if(!width || !height || width>16384 || height>16384 || (word(112)&0x0020fe00))throw std::runtime_error("DDS must be a bounded 2D image: "+path);
    uint32_t fourcc=word(84);size_t offset=128;int type=fourcc==0x31545844?1:fourcc==0x33545844?2:fourcc==0x35545844?3:0;
    if(fourcc==0x30315844){const auto format=word(128);if(word(132)!=3 || word(136)!=0 || word(140)!=1)throw std::runtime_error("DDS DX10 arrays/cubes are unsupported: "+path);type=(format==71 || format==72)?1:(format==74 || format==75)?2:(format==77 || format==78)?3:0;offset=148;if(!type)throw std::runtime_error("DDS DX10 format is not BC1/BC2/BC3: "+path);}
    ImageRGBA8 result{width,height,std::vector<uint8_t>(size_t(width)*height*4)};
    if(!type){if(fourcc || !(word(80)&64) || word(88)!=32 || bytes.size()-std::min(bytes.size(),offset)<size_t(width)*height*4)throw std::runtime_error("DDS format is not RGBA32/BC1/BC2/BC3: "+path);
        std::array<uint32_t,4> masks{word(92),word(96),word(100),word(104)};for(size_t p=0;p<size_t(width)*height;++p){auto value=word(offset+p*4);for(size_t c=0;c<4;++c){uint32_t mask=masks[c],shift=0;if(!mask){result.pixels[p*4+c]=c==3?255:0;continue;}while(!(mask&1)){mask>>=1;++shift;}result.pixels[p*4+c]=uint8_t(uint64_t((value>>shift)&mask)*255/mask);}}return result;
    }
    const size_t blockBytes=type==1?8:16,bx=(width+3)/4,by=(height+3)/4;if(bytes.size()<offset+bx*by*blockBytes)throw std::runtime_error("Truncated DDS block data: "+path);
    for(size_t y=0;y<by;++y)for(size_t x=0;x<bx;++x){const auto* block=bytes.data()+offset+(y*bx+x)*blockBytes;const auto* color=block+(type==1?0:8);uint16_t a=uint16_t(color[0])|uint16_t(color[1])<<8,b=uint16_t(color[2])|uint16_t(color[3])<<8;std::array<std::array<uint8_t,4>,4> palette{};
        auto rgb=[](uint16_t v){return std::array<uint8_t,4>{uint8_t(((v>>11)&31)<<3 | ((v>>11)&31)>>2),uint8_t(((v>>5)&63)<<2 | ((v>>5)&63)>>4),uint8_t((v&31)<<3 | (v&31)>>2),255};};palette[0]=rgb(a);palette[1]=rgb(b);
        for(size_t c=0;c<3;++c){palette[2][c]=type==1&&a<=b?(palette[0][c]+palette[1][c])/2:(2*palette[0][c]+palette[1][c])/3;palette[3][c]=type==1&&a<=b?0:(palette[0][c]+2*palette[1][c])/3;}palette[2][3]=255;palette[3][3]=type==1&&a<=b?0:255;
        uint32_t codes=uint32_t(color[4])|uint32_t(color[5])<<8|uint32_t(color[6])<<16|uint32_t(color[7])<<24;uint64_t alphaBits=0;std::array<uint8_t,8> alpha{};
        if(type==2)for(int i=0;i<8;++i)alphaBits|=uint64_t(block[i])<<(8*i);
        if(type==3){alpha[0]=block[0];alpha[1]=block[1];for(int i=2;i<(alpha[0]>alpha[1]?8:6);++i)alpha[i]=uint8_t((((alpha[0]>alpha[1]?8:6)-i)*alpha[0]+(i-1)*alpha[1])/(alpha[0]>alpha[1]?7:5));if(alpha[0]<=alpha[1]){alpha[6]=0;alpha[7]=255;}for(int i=0;i<6;++i)alphaBits|=uint64_t(block[i+2])<<(8*i);}
        for(size_t row=0;row<4;++row)for(size_t col=0;col<4;++col){const size_t i=row*4+col;if(x*4+col>=width || y*4+row>=height)continue;auto value=palette[(codes>>(2*i))&3];if(type==2)value[3]=uint8_t((alphaBits>>(4*i))&15)*17;if(type==3)value[3]=alpha[(alphaBits>>(3*i))&7];std::memcpy(result.pixels.data()+((y*4+row)*width+x*4+col)*4,value.data(),4);}
    }return result;
}
}
