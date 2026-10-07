#include "renderer/rhi/GpuPostProcessor.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
namespace render {
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
bool close(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(std::abs(int(a[i])-int(b[i]))>2)return false;return true;}
}
void validatePostProcessing(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    using namespace rhi;
    Resources owned(device);GpuPostProcessor processor(device,directory);
    uint32_t width=65,height=49;
    auto input=owned.texture({width,height,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Post HDR fixture"});auto inputView=owned.view(input);
    auto output=owned.texture({width,height,Format::RGBA8UNorm,TextureUsage::Sampled|TextureUsage::ColorAttachment|TextureUsage::CopySource,"Post fixture output"});auto outputView=owned.view(output);
    auto depth=owned.texture({width,height,Format::Depth32Float,TextureUsage::DepthAttachment|TextureUsage::Sampled,"Post fixture depth"});auto depthView=owned.view(depth);
    std::vector<float> hdr(size_t(width)*height*4,1);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){size_t i=4*(size_t(y)*width+x);hdr[i]=x==width/2?8.f:.02f;hdr[i+1]=y>height/2?.1f:.4f;hdr[i+2]=float(x)/width;}
    device->writeTextureFloat(input,hdr.data(),hdr.size()*sizeof(float));
    PostProcessView view;view.view=glm::lookAt(glm::vec3(0,0,10),glm::vec3(0),glm::vec3(0,1,0));view.cameraPosition={0,0,10};
    glm::mat4 conversion(1);conversion[2][2]=.5f;conversion[3][2]=.5f;
    const auto projection=conversion*glm::ortho(-2.f,2.f,-2.f,2.f,.1f,50.f);view.viewProjection=view.unjitteredViewProjection=projection*view.view;
    {auto commands=device->createCommandList();RenderPassDesc pass;pass.color=outputView;pass.depth=depthView;auto clip=view.viewProjection*glm::vec4(0,0,0,1);pass.clearDepth=clip.z/clip.w;
        commands.beginRenderPass(pass);commands.endRenderPass();device->submit(commands);}
    auto render=[&](PostProcessSettings s,bool tone=true,float exposure=1){
        {Resources frame(device);auto commands=device->createCommandList();processor.record(frame,commands,width,height,inputView,depthView,outputView,s,view,exposure,2.2f,tone);device->submit(commands);processor.commit();device->waitIdle();}
        device->waitIdle();return device->readTexture(output);
    };
    PostProcessSettings s;const auto base=render(s);
    const auto baselineTextureBytes=device->resourceMemory().textureBytes;
    for(size_t i=0;i<hdr.size();i+=4){check(base[i+3]==255,"Post output alpha changed");for(int c=0;c<3;++c){float expected=std::pow(1-std::exp(-hdr[i+c]),1/2.2f)*255;check(std::abs(base[i+c]-expected)<=2,"Default post output differs from original exponential mapping / UV orientation");}}
    auto linear=render(s,false);for(size_t i=0;i<hdr.size();i+=4)for(int c=0;c<3;++c)check(std::abs(linear[i+c]-255*std::pow(std::min(1.f,hdr[i+c]),1/2.2f))<=2,"Tone bypass changed");
    s.bloom=true;s.bloomStrength=.6f;const auto bloom=render(s);size_t beside=4*(size_t(height/2)*width+width/2+3);check(bloom[beside]>base[beside]+10,"Bloom did not spread HDR highlight energy");
    // A flat bright field must have level-count-independent energy, including odd edges.
    std::vector<float> flat(hdr.size(),1);for(size_t i=0;i<flat.size();i+=4)flat[i]=flat[i+1]=flat[i+2]=2;
    device->writeTextureFloat(input,flat.data(),flat.size()*sizeof(float));
    s.bloomThreshold=0;s.bloomKnee=0;auto flatBloom=render(s);
    const float expectedFlat=255*std::pow(1-std::exp(-2*(1+s.bloomStrength)),1/2.2f);
    for(size_t i=0;i<flatBloom.size();i+=4)for(int c=0;c<3;++c)check(std::abs(flatBloom[i+c]-expectedFlat)<=2,"Bloom pyramid inflated flat-field energy / odd edge");
    device->writeTextureFloat(input,hdr.data(),hdr.size()*sizeof(float));
    s.enabled=false;check(render(s)==base,"Master bypass retained bloom");s=PostProcessSettings{};check(render(s)==base,"Bloom disable retained a previous intermediate");
    check(device->resourceMemory().textureBytes==baselineTextureBytes,"Bloom disable retained intermediate textures");
    for(auto mapper:{ToneMapper::ACES,ToneMapper::Reinhard,ToneMapper::Linear}){s.toneMapper=mapper;check(render(s)!=base,"Tone mapper switch has no effect");}
    s=PostProcessSettings{};s.depthOfField=true;s.focusDistance=10;check(close(render(s),base),"Focused plane was blurred");s.focusDistance=20;s.focusRange=2;check(render(s)[beside]>base[beside]+10,"Defocused plane did not spread the highlight");
    s=PostProcessSettings{};s.motionBlur=true;s.shutter=1;processor.reset();check(close(render(s),base),"Motion blur used invalid history");check(close(render(s),base),"Stationary camera was blurred");
    glm::mat4 jitter(1);jitter[3].x=.3f/width;jitter[3].y=-.2f/height;
    view.viewProjection=jitter*view.unjitteredViewProjection;
    check(close(render(s),base),"TSAA jitter created camera motion blur");
    view.viewProjection=view.unjitteredViewProjection;
    view.cameraPosition.x=.3f;view.view=glm::lookAt(view.cameraPosition,glm::vec3(.3f,0,0),glm::vec3(0,1,0));view.viewProjection=view.unjitteredViewProjection=projection*view.view;
    const auto moving=render(s);check(moving!=base,"Camera motion blur ignored camera translation");
    processor.reset();check(close(render(s),base),"Camera cut retained motion history");
    for(int effect=0;effect<6;++effect){s=PostProcessSettings{};if(effect==0){s.colorGrading=true;s.saturation=0;}if(effect==1)s.fxaa=true;if(effect==2){s.sharpen=true;s.sharpness=1;}if(effect==3){s.vignette=true;s.vignetteStrength=1;}if(effect==4){s.chromaticAberration=true;s.chromaticPixels=8;}if(effect==5)s.filmGrain=true;
        check(render(s)!=base,"Display effect switch had no effect");s.enabled=false;check(render(s)==base,"Display effect bypass retained pixels");}
    s=PostProcessSettings{};s.filmGrain=true;view.time=0;auto grain=render(s);view.time=.5f;check(render(s)!=grain,"Film grain did not animate");
    s=PostProcessSettings{};s.bloom=true;s.depthOfField=true;s.motionBlur=true;s.fxaa=true;s.colorGrading=true;s.vignette=true;s.sharpen=true;s.chromaticAberration=true;s.filmGrain=true;
    auto combined=render(s);check(combined.size()==size_t(width)*height*4,"Combined post extent changed");for(size_t i=3;i<combined.size();i+=4)check(combined[i]==255,"Combined post alpha changed");
    // Tiny and odd dimensions exercise pyramid edges and resource replacement.
    width=3;height=1;auto tiny=owned.texture({width,height,Format::RGBA8UNorm,TextureUsage::ColorAttachment|TextureUsage::CopySource,"Post tiny output"});output=tiny;outputView=owned.view(tiny);
    check(render(s).size()==12,"Post resize failed at tiny odd extent");
    s.focusRange=std::numeric_limits<float>::quiet_NaN();bool rejected=false;try{render(s);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Post accepted nonfinite parameters");
    std::cout<<"Post processing: default CPU tone reference, bypass, HDR bloom, tone curves, focus plane, defocus, camera/static/cut blur, display switches, animated grain, combined effects, odd/tiny resize and invalid parameters passed\n";
}
}
