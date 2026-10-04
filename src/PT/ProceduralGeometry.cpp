#include "PT/ProceduralCapture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <glm/gtc/matrix_inverse.hpp>
#include <stdexcept>
namespace pt {
namespace {
void validate(const CaptureOptions &o){if((o.terrainGrid&&(o.terrainGrid<2||o.terrainGrid>1025))||o.textureExtent<64||o.textureExtent>4096||(o.oceanGrid&&(o.oceanGrid<2||o.oceanGrid>1025)))throw std::invalid_argument("PT capture: invalid geometry/texture resolution");}
class Pages {
    const render::VirtualTextureSource &source;uint32_t mip;
    std::map<std::pair<uint32_t,uint32_t>,render::VirtualTextureSource::Page> pages;
public:
    Pages(const render::VirtualTextureSource &s,uint32_t m=0):source(s),mip(m){if(!s.readPage||!s.ioMutex||s.extent<64)throw std::invalid_argument("PT capture: invalid VT source");}
    glm::vec4 pixel(int x,int y,uint32_t layer){
        const uint32_t n=source.extent>>mip;x=std::clamp(x,0,int(n)-1);y=std::clamp(y,0,int(n)-1);auto key=std::make_pair(uint32_t(x)/64,uint32_t(y)/64);auto it=pages.find(key);
        if(it==pages.end()){std::lock_guard<std::mutex> lock(*source.ioMutex);it=pages.emplace(key,source.readPage(mip,key.first,key.second)).first;}
        const size_t offset=(size_t(y%64+2)*68+size_t(x%64+2))*4;const auto &p=it->second;
        if(layer>=p.size()||layer>=source.formats.size())throw std::invalid_argument("PT capture: missing VT layer");
        if(source.formats[layer]==rhi::Format::RGBA32Float){if(p[layer].size()!=68*68*16)throw std::invalid_argument("PT capture: truncated height page");glm::vec4 v;std::memcpy(&v,p[layer].data()+offset*4,16);if(!std::isfinite(v.x))throw std::invalid_argument("PT capture: nonfinite height");return v;}
        if(source.formats[layer]!=rhi::Format::RGBA8UNorm||p[layer].size()!=68*68*4)throw std::invalid_argument("PT capture: invalid material page");auto a=p[layer].data()+offset;return glm::vec4(a[0],a[1],a[2],a[3])/255.f;
    }
    glm::vec4 sample(glm::vec2 uv,uint32_t layer,bool height){float n=float(source.extent>>mip);auto p=glm::clamp(uv,0.f,1.f)*(height?n-1:n)-(height?0.f:.5f);int x=int(std::floor(p.x)),y=int(std::floor(p.y));return glm::mix(glm::mix(pixel(x,y,layer),pixel(x+1,y,layer),p.x-x),glm::mix(pixel(x,y+1,layer),pixel(x+1,y+1,layer),p.x-x),p.y-y);}
};
void gridIndices(render::MeshPayload &m,uint32_t n){for(uint32_t y=0;y+1<n;++y)for(uint32_t x=0;x+1<n;++x){uint32_t a=y*n+x,b=a+1,c=a+n,d=c+1;m.indices.insert(m.indices.end(),{a,d,b,a,c,d});}}
uint8_t byte(float f){return uint8_t(std::round(glm::clamp(f,0.f,1.f)*255));}
glm::vec3 periodic(const std::vector<float> &image,uint32_t n,glm::vec2 uv){auto p=glm::fract(uv)*float(n);int x=int(std::floor(p.x)),y=int(std::floor(p.y));auto at=[&](int a,int b){size_t i=(size_t((b%int(n)+n)%n)*n+(a%int(n)+n)%n)*4;return glm::vec3(image[i],image[i+1],image[i+2]);};return glm::mix(glm::mix(at(x,y),at(x+1,y),p.x-x),glm::mix(at(x,y+1),at(x+1,y+1),p.x-x),p.y-y);}
glm::vec4 imageSample(const render::ImageRGBA8 &image,glm::vec2 uv,bool repeat=false){
    const int w=int(image.width),h=repeat?w:int(image.height);
    if(w<=0||h<=0||(repeat&&image.height<image.width)||image.pixels.size()!=size_t(image.width)*image.height*4)throw std::invalid_argument("PT capture: invalid beach image");
    auto p=(repeat?glm::fract(uv):glm::clamp(uv,0.f,1.f))*glm::vec2(w,h)-.5f;int x=int(std::floor(p.x)),y=int(std::floor(p.y));
    auto at=[&](int a,int b){a=repeat?(a%w+w)%w:std::clamp(a,0,w-1);b=repeat?(b%h+h)%h:std::clamp(b,0,h-1);const auto *c=image.pixels.data()+(size_t(b)*w+a)*4;return glm::vec4(c[0],c[1],c[2],c[3])/255.f;};
    return glm::mix(glm::mix(at(x,y),at(x+1,y),p.x-x),glm::mix(at(x,y+1),at(x+1,y+1),p.x-x),p.y-y);
}
void bakeShoreline(const render::SnapshotTerrain &terrain,Pages &height,render::MaterialPayload &material,render::MaterialParameters &parameters){
    const auto &extension=terrain.extension;const auto &images=terrain.source->shorelineImages;
    if(!images[0]&&!(extension.features.x&int(render::MaterialFeature::Shoreline)))return;
    for(auto &image:images)if(!image)throw std::invalid_argument("PT capture: missing beach material");
    const auto H=extension.shoreHeight,S=extension.shoreSurface;
    for(auto v:{H,S})for(int k=0;k<4;++k)if(!std::isfinite(v[k]))throw std::invalid_argument("PT capture: nonfinite beach parameters");
    if(S.x<=0||H.y<=0||H.z+H.w<=0||S.y>=S.z)throw std::invalid_argument("PT capture: invalid beach parameters");
    const uint32_t n=material.images[0]->width;std::array<std::shared_ptr<render::ImageRGBA8>,4> result;
    for(auto &image:result){image=std::make_shared<render::ImageRGBA8>();image->width=image->height=n;image->pixels.resize(size_t(n)*n*4);}
    const float delta=1.f/(terrain.source->height.extent-1);const auto normalMatrix=glm::inverseTranspose(glm::mat3(terrain.model));
    for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x){glm::vec2 uv{(x+.5f)/n,(y+.5f)/n},at{uv.x,1-uv.y},lo=glm::max(at-glm::vec2(delta),glm::vec2(0)),hi=glm::min(at+glm::vec2(delta),glm::vec2(1));
        float h=height.sample(at,0,true).x,dx=(height.sample({hi.x,at.y},0,true).x-height.sample({lo.x,at.y},0,true).x)/(2*(hi.x-lo.x)),dz=(height.sample({at.x,hi.y},0,true).x-height.sample({at.x,lo.y},0,true).x)/(2*(hi.y-lo.y));
        auto N=glm::normalize(normalMatrix*glm::vec3(-dx,1,-dz));auto position=glm::vec3(terrain.model*glm::vec4(at.x*2-1,h,at.y*2-1,1));float altitude=position.y-H.x;
        float weight=imageSample(*images[3],uv).r*(1-glm::smoothstep(H.y*.55f,H.y,altitude))*glm::smoothstep(S.y,S.z,std::abs(N.y))*glm::smoothstep(-4.f,-.5f,altitude);
        float wet=1-glm::smoothstep(-H.z,H.w,altitude);glm::vec2 beachUV=glm::vec2(position.x,-position.z)/S.x;
        auto base=imageSample(*material.images[0],uv)*parameters.albedoAlpha;auto sand=imageSample(*images[0],beachUV,true);auto orm=imageSample(*images[2],beachUV,true);
        auto color=glm::pow(glm::mix(glm::pow(glm::vec3(base),glm::vec3(2.2f)),glm::pow(glm::vec3(sand),glm::vec3(2.2f))*glm::mix(1.f,.45f,wet),weight),glm::vec3(1/2.2f));
        float rough=glm::clamp(glm::mix(parameters.factors.y,glm::mix(orm.g,std::max(.28f,orm.g*.5f),wet),weight),.045f,1.f);
        auto T=glm::vec3(1,0,0)-N*N.x;auto mapped=N;
        if(glm::dot(T,T)>1e-8f){T=glm::normalize(T);auto sample=glm::normalize(glm::vec3(imageSample(*images[1],beachUV,true))*2.f-1.f);sample.x*=S.w;sample.y*=S.w;mapped=glm::normalize(glm::mix(N,glm::normalize(T*sample.x+glm::cross(N,T)*sample.y+N*sample.z),weight));}
        auto tangent=glm::mat3(terrain.model)*glm::vec3(1,dx,0),bitangent=glm::mat3(terrain.model)*glm::vec3(0,-dz,-1);tangent=glm::normalize(tangent-N*glm::dot(tangent,N));bitangent=glm::normalize(bitangent-N*glm::dot(bitangent,N));
        auto encoded=glm::normalize(glm::inverse(glm::mat3(tangent,bitangent,N))*mapped)*.5f+.5f;size_t i=(size_t(y)*n+x)*4;
        for(int k=0;k<3;++k){result[0]->pixels[i+k]=byte(color[k]);result[1]->pixels[i+k]=byte(encoded[k]);}result[0]->pixels[i+3]=byte(base.a);result[1]->pixels[i+3]=255;result[2]->pixels[i+3]=255;result[3]->pixels[i+1]=byte(rough);result[3]->pixels[i+3]=255;
    }
    for(int i=0;i<4;++i)material.images[i]=result[i];parameters.albedoAlpha=glm::vec4(1);parameters.factors.x=0;parameters.factors.y=1;parameters.emissiveNormal.w=1;
}
void validate(const OceanSamples &s){if(!s.size||s.displacement.size()!=size_t(s.size)*s.size*4||s.normal.size()!=s.displacement.size()||s.foam.size()!=s.displacement.size())throw std::invalid_argument("PT capture: invalid FFT readback");for(auto *v:{&s.displacement,&s.normal,&s.foam})for(float f:*v)if(!std::isfinite(f))throw std::invalid_argument("PT capture: nonfinite FFT readback");}
}
void validateCaptureOptions(const CaptureOptions &options){validate(options);}
render::SnapshotDraw freezeTerrain(const render::SnapshotTerrain &terrain,const CaptureOptions &options){
    validate(options);if(!terrain.source||!terrain.source->height.heightField)throw std::invalid_argument("PT capture: missing terrain height");const auto &source=*terrain.source;const float determinant=glm::determinant(glm::mat3(terrain.model));if(!std::isfinite(determinant)||std::abs(determinant)<1e-15f)throw std::invalid_argument("PT capture: singular terrain transform");Pages height(source.height);
    auto mesh=std::make_shared<render::MeshPayload>();const uint32_t n=options.terrainGrid?options.terrainGrid:std::min(source.height.extent,1025u);mesh->vertices.reserve(size_t(n)*n);const float delta=1.f/(source.height.extent-1);
    for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x){glm::vec2 uv{float(x)/(n-1),float(y)/(n-1)},lo=glm::max(uv-glm::vec2(delta),glm::vec2(0)),hi=glm::min(uv+glm::vec2(delta),glm::vec2(1));float h=height.sample(uv,0,true).x;
        float dx=(height.sample({hi.x,uv.y},0,true).x-height.sample({lo.x,uv.y},0,true).x)/(2*(hi.x-lo.x)),dz=(height.sample({uv.x,hi.y},0,true).x-height.sample({uv.x,lo.y},0,true).x)/(2*(hi.y-lo.y));mesh->vertices.push_back({{uv.x*2-1,h,uv.y*2-1},glm::normalize(glm::vec3(-dx,1,-dz)),{uv.x,1-uv.y}});}
    gridIndices(*mesh,n);auto material=std::make_shared<render::MaterialPayload>();uint32_t mip=0;while((source.material.extent>>mip)>options.textureExtent)++mip;Pages colors(source.material,mip);const uint32_t extent=source.material.extent>>mip;
    for(uint32_t layer=0;layer<5;++layer){auto image=std::make_shared<render::ImageRGBA8>();image->width=image->height=extent;image->pixels.resize(size_t(extent)*extent*4);for(uint32_t y=0;y<extent;++y)for(uint32_t x=0;x<extent;++x){auto c=colors.pixel(x,y,layer);for(int k=0;k<4;++k)image->pixels[(size_t(y)*extent+x)*4+k]=byte(c[k]);}material->images[layer]=image;}
    render::SnapshotDraw draw;draw.objectId=source.id;draw.mesh=mesh;draw.material=material;draw.model=terrain.model;draw.parameters=terrain.parameters;bakeShoreline(terrain,height,*material,draw.parameters);draw.pathTracingKind=1;return draw;
}
render::SnapshotDraw freezeOcean(const render::OceanSurfaceSettings &s,const OceanSamples &large,const OceanSamples &detail,const CaptureOptions &options){
    validate(options);validate(large);if(s.detailWaves)validate(detail);if(!std::isfinite(s.seaLevel)||!std::isfinite(s.spectrum.length)||s.spectrum.length<=0||glm::any(glm::lessThan(s.absorption,glm::vec3(0)))||!std::isfinite(glm::length(s.absorption))||glm::any(glm::lessThan(s.scattering,glm::vec3(0)))||!std::isfinite(glm::length(s.scattering))||!std::isfinite(s.anisotropy)||std::abs(s.anisotropy)>=.99f)throw std::invalid_argument("PT capture: invalid water optics");
    const float length=s.surfaceLength>0?s.surfaceLength:s.spectrum.length;const uint32_t n=options.oceanGrid?options.oceanGrid:s.meshSize;if(n<2||n>1025||!std::isfinite(length)||length<=0)throw std::invalid_argument("PT capture: invalid ocean grid");
    auto mesh=std::make_shared<render::MeshPayload>();mesh->vertices.reserve(size_t(n)*n);
    for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x){float u=float(x)/(n-1),v=float(y)/(n-1);glm::vec3 p{(u-.5f)*length,0,(v-.5f)*length};glm::vec2 uv=glm::vec2(p.x,p.z)/s.spectrum.length+glm::vec2(.5f);auto displacement=periodic(large.displacement,large.size,uv);auto normal=periodic(large.normal,large.size,uv);
        if(s.detailWaves){auto duv=glm::vec2(p.x,p.z)/32.f+glm::vec2(.5f);displacement+=periodic(detail.displacement,detail.size,duv);auto dn=periodic(detail.normal,detail.size,duv);auto slope=glm::vec2(normal.x,normal.z)/std::max(normal.y,.1f)+glm::vec2(dn.x,dn.z)/std::max(dn.y,.1f);normal={slope.x,1,slope.y};}
        p+=displacement;p.y+=s.seaLevel;mesh->vertices.push_back({p,glm::normalize(normal),{u,1-v}});}
    gridIndices(*mesh,n);auto material=std::make_shared<render::MaterialPayload>();
    if(s.waterMask){auto image=std::make_shared<render::ImageRGBA8>(*s.waterMask);if(!image->width||!image->height||image->pixels.size()!=size_t(image->width)*image->height*4)throw std::invalid_argument("PT capture: invalid shoreline mask");for(size_t i=0;i<image->pixels.size();i+=4){image->pixels[i+3]=image->pixels[i];image->pixels[i]=image->pixels[i+1]=image->pixels[i+2]=255;}material->images[0]=image;}
    // Retain the periodic FFT normal field independently of the much larger lake mask UVs.
    const uint32_t extent=std::min(options.textureExtent,std::max(large.size,256u));auto normalMap=std::make_shared<render::ImageRGBA8>();normalMap->width=normalMap->height=extent;normalMap->pixels.resize(size_t(extent)*extent*4);auto foamMap=std::make_shared<render::ImageRGBA8>();foamMap->width=foamMap->height=extent;foamMap->pixels.resize(size_t(extent)*extent*4);
    const float scale=length/s.spectrum.length;
    for(uint32_t y=0;y<extent;++y)for(uint32_t x=0;x<extent;++x){glm::vec2 uv{(x+.5f)/extent,(y+.5f)/extent};glm::vec2 world{(uv.x-.5f*scale)*s.spectrum.length,(.5f*scale-uv.y)*s.spectrum.length};auto N=periodic(large.normal,large.size,glm::vec2(world.x,world.y)/s.spectrum.length+glm::vec2(.5f));float foam=periodic(large.foam,large.size,world/s.spectrum.length+glm::vec2(.5f)).x;if(s.detailWaves)foam=1-(1-foam)*(1-periodic(detail.foam,detail.size,world/32.f+glm::vec2(.5f)).x);size_t fi=(size_t(y)*extent+x)*4;foamMap->pixels[fi]=byte(foam);foamMap->pixels[fi+3]=255;
        if(s.detailWaves){auto D=periodic(detail.normal,detail.size,world/32.f+glm::vec2(.5f));auto slope=glm::vec2(N.x,N.z)/std::max(N.y,.1f)+glm::vec2(D.x,D.z)/std::max(D.y,.1f);N={slope.x,1,slope.y};}N=glm::normalize(N);size_t i=(size_t(y)*extent+x)*4;normalMap->pixels[i]=byte(N.x*.5f+.5f);normalMap->pixels[i+1]=byte(-N.z*.5f+.5f);normalMap->pixels[i+2]=byte(N.y*.5f+.5f);normalMap->pixels[i+3]=255;}
    material->images[1]=normalMap;material->images[2]=foamMap;render::SnapshotDraw draw;draw.objectId=s.id;draw.mesh=mesh;draw.material=material;draw.parameters.albedoAlpha=glm::vec4(1);draw.parameters.factors={0,.045f,1,s.waterMask?.5f:0};draw.parameters.emissiveNormal.w=1;
    draw.pathTracingIor=s.refraction?1.333f:0;draw.pathTracingAbsorption=s.absorption;draw.pathTracingScattering=s.refraction?s.scattering:glm::vec3(0);draw.pathTracingAnisotropy=s.anisotropy;draw.pathTracingNormalScale=scale;draw.pathTracingKind=3;
    if(!s.refraction)draw.parameters.albedoAlpha=glm::vec4(glm::pow(s.deep,glm::vec3(1/2.2f)),1);
    return draw;
}
}
