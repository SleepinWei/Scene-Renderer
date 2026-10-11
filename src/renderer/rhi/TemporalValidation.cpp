#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/GpuImageCache.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>
namespace render {
namespace{void check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}}
void validateTemporalRhi(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    if(!device->computeLimits().maxStorageImages)return;
    {
        const ImageRGBA8 checker{2,1,{0,0,0,255,255,255,255,255}};
        const auto color=buildImageMipChain(checker,{ImageMipFilter::Color,0});
        const auto data=buildImageMipChain(checker,{ImageMipFilter::Linear,0});
        const auto rough=buildImageMipChain(checker,{ImageMipFilter::Roughness,0});
        check(std::abs(int(color[0].pixels[0])-186)<=1&&data[0].pixels[0]==128,"Mip checker reduction lost linear color energy or data values");
        check(std::abs(int(rough[0].pixels[1])-214)<=1,"Roughness mips lost GGX slope variance");
        const ImageRGBA8 odd{3,1,{0,0,0,255,0,0,0,255,255,255,255,255}};
        check(buildImageMipChain(odd,{ImageMipFilter::Linear,0})[0].pixels[0]==85,"NPOT mip dropped the final texel");
        if(device->supportsTextureSubresources()){
            auto cache=GpuImageCache::forDevice(device);auto source=std::make_shared<const ImageRGBA8>(checker);
            auto pending=cache->acquire(source,true,{ImageMipFilter::Color,0});
            check(pending&&pending->bytes()==12&&!pending->ready(),"Deferred mip allocation or accounting differs");
            check(cache->upload(pending,8)==8&&!pending->ready(),"Deferred material became ready before its mip tail");
            check(cache->upload(pending,4)==4&&pending->ready(),"Deferred mip tail did not fit its byte budget");
            check(device->readTextureSubresource(pending->texture(),{1,0})==color[0].pixels,"GPU mip differs from analytic checker average");
            auto linear=cache->acquire(source,false,{ImageMipFilter::Linear,0});
            check(linear!=pending&&device->readTextureSubresource(linear->texture(),{1,0})==data[0].pixels,"Image cache aliased color and normal/data mip policies");
        }
        std::cout<<"RHI mip color energy, data/roughness semantics, NPOT edges, GPU readback and deferred byte budgets passed\n";
    }
    std::vector<MeshVertex> vertices{{{-.2f,-.2f,0},{0,0,1},{0,1}},{{.2f,-.2f,0},{0,0,1},{1,1}},{{.2f,.2f,0},{0,0,1},{1,0}},{{-.2f,.2f,0},{0,0,1},{0,0}}};auto mesh=std::make_shared<GpuMesh>(device,vertices,std::vector<uint32_t>{0,1,2,0,2,3});MaterialDesc m;m.parameters.albedoAlpha={0,0,0,1};m.parameters.emissiveNormal={4,.2f,.1f,0};auto material=std::make_shared<GpuMaterial>(device,m);
    FrameData f;f.taa=true;f.view=glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0));glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;f.viewProjection=correction*glm::ortho(-1.f,1.f,-1.f,1.f,.1f,10.f)*f.view;
    ForwardPbrRenderer renderer(device,directory,64,64,PbrPath::Scene);std::vector<DrawPacket> draws{{mesh,material,glm::mat4(1),42}};
    for(int i=0;i<4;i++){renderer.render(f,draws);auto hdr=renderer.readHDR();for(float v:hdr)check(std::isfinite(v),"Temporal resolve nonfinite");check(std::abs(hdr[(32*64+32)*4]-4)<.01f,"Temporal static HDR changed/clamped");}
    draws[0].model[3].x=.7f;renderer.render(f,draws);auto moved=renderer.readHDR();check(moved[(32*64+32)*4]<.01f && moved[(32*64+54)*4]>3.9f,"Temporal object motion/disocclusion left an emissive trail");
    renderer.resize(48,32);renderer.render(f,draws);check(renderer.readOutput().size()==48*32*4,"Temporal resize failed");f.taa=false;renderer.render(f,draws);f.taa=true;renderer.render(f,draws);for(float v:renderer.readHDR())check(std::isfinite(v),"Temporal history reset failed");
    f.historyKey=2;renderer.render(f,draws);for(float v:renderer.readHDR())check(std::isfinite(v),"Temporal scene change reset failed");
    {
        // A synthetic optical surface has fractional motion confidence. An
        // out-of-screen vector must reject history even below the old .5 gate.
        using namespace rhi;Resources inputs(device);constexpr uint32_t w=32,h=8;
        auto image=[&](const char* name){return inputs.texture({w,h,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,name});};
        auto color=image("Temporal confidence color"),depth=image("Temporal confidence depth"),motion=image("Temporal confidence motion");
        auto cv=inputs.view(color),dv=inputs.view(depth),mv=inputs.view(motion);
        std::vector<glm::vec4> seed(w*h,glm::vec4(.6f,0,0,1)),current(w*h),motions(w*h);
        auto clip=f.viewProjection*glm::vec4(0,0,0,1);float z=clip.z/clip.w;
        auto world=glm::inverse(f.viewProjection)*glm::vec4(0,0,z,1);world/=world.w;
        float linearDepth=-(f.view*world).z;std::vector<glm::vec4> depths(w*h,glm::vec4(z,0,0,0));
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)current[y*w+x]={((x+y)%2)? .8f:.2f,0,0,1};
        device->writeTextureFloat(depth,reinterpret_cast<const float*>(depths.data()),depths.size()*16);
        GpuTemporal temporal(device,directory);temporal.resize(w,h);
        auto record=[&](){Resources frame(device);auto commands=device->createCommandList();temporal.record(frame,commands,f,cv,dv,mv);device->submit(commands);temporal.commit(f);};
        {
            // A linear ramp sampled on a raster grid shifted by 1/4 pixel must
            // return to the fixed output grid, even on the first HQ frame.
            const auto saved=f;f.temporalJitter={.25f,0};glm::mat4 shift(1);shift[3].x=.5f/w;f.viewProjection=shift*f.viewProjection;
            std::vector<glm::vec4> ramp(w*h);for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)ramp[y*w+x]={.025f*(float(x)-.25f),0,0,1};
            device->writeTextureFloat(color,reinterpret_cast<const float*>(ramp.data()),ramp.size()*16);
            device->writeTextureFloat(motion,reinterpret_cast<const float*>(motions.data()),motions.size()*16);
            record();check(std::abs(temporal.read()[4*(4*w+16)]-.4f)<.001f,"HQ temporal output retained the current raster jitter phase");f=saved;
            std::cout<<"RHI temporal fixed output grid / subpixel jitter phase restoration passed\n";
        }
        auto resolve=[&](float confidence,float dx){
            temporal.reset();std::fill(motions.begin(),motions.end(),glm::vec4(0));
            device->writeTextureFloat(color,reinterpret_cast<const float*>(seed.data()),seed.size()*16);
            device->writeTextureFloat(motion,reinterpret_cast<const float*>(motions.data()),motions.size()*16);record();
            std::fill(motions.begin(),motions.end(),glm::vec4(dx,0,linearDepth,confidence));
            device->writeTextureFloat(color,reinterpret_cast<const float*>(current.data()),current.size()*16);
            device->writeTextureFloat(motion,reinterpret_cast<const float*>(motions.data()),motions.size()*16);record();
            return temporal.read()[4*(4*w+16)];
        };
        check(std::abs(resolve(.25f,.75f)-.2f)<.002f,"Low-confidence optical motion fell back to static depth reprojection");
        const auto full=resolve(1,0),low=resolve(.25f,0);
        check(low>.21f&&low<full-.03f,"Optical confidence did not reduce historical shading weight");
        check(std::abs(resolve(-1,0)-.2f)<.002f,"Reactive optical surface retained an obsolete reflection/refraction image");
        check(resolve(0,.75f)>.23f,"Static depth fallback lost valid temporal accumulation");
        // Alternating bright/dark samples on the same static water footprint
        // must average in linear HDR. Compressed-domain averaging loses flux.
        temporal.reset();std::fill(motions.begin(),motions.end(),glm::vec4(0,0,linearDepth,4));
        device->writeTextureFloat(motion,reinterpret_cast<const float*>(motions.data()),motions.size()*16);
        for(int i=0;i<4;++i){float value=i%2?10.f:.1f;std::vector<glm::vec4> flash(w*h,glm::vec4(value,value,value,1));
            device->writeTextureFloat(color,reinterpret_cast<const float*>(flash.data()),flash.size()*16);record();}
        check(std::abs(temporal.read()[4*(4*w+16)]-5.05f)<.015f,"Physical water TAA lost linear HDR glint energy");
        // A moving subpixel jitter must not mix a glint with its dark/bright
        // neighbours in a frozen film. This catches repeated reconstruction.
        {const auto saved=f;temporal.reset();
            const float jitter[]={0.f,.45f,-.43f,.35f};
            std::fill(motions.begin(),motions.end(),glm::vec4(0,0,linearDepth,4));
            device->writeTextureFloat(motion,reinterpret_cast<const float*>(motions.data()),motions.size()*16);
            for(int i=0;i<4;++i){f.temporalJitter={jitter[i],0};glm::mat4 shift(1);shift[3].x=2*jitter[i]/w;f.viewProjection=shift*saved.viewProjection;
                std::vector<glm::vec4> pulse(w*h,glm::vec4(i%2?.1f:10.f,0,0,1));pulse[4*w+16]=glm::vec4(i%2?10.f:.1f,0,0,1);
                device->writeTextureFloat(color,reinterpret_cast<const float*>(pulse.data()),pulse.size()*16);record();}
            check(std::abs(temporal.read()[4*(4*w+16)]-5.05f)<.015f,"Frozen glint film resampled subpixel jitter / neighbouring radiance");f=saved;
        }
        check(std::abs(resolve(4,.75f)-.2f)<.002f,"Physical water accepted out-of-screen history");
        check(std::abs(resolve(-1,0)-.2f)<.002f,"Reactive water retained history");
        std::cout<<"RHI physical water temporal linear HDR glint average and history rejection passed\n";
        std::cout<<"RHI temporal fractional optical motion, out-of-screen rejection, confidence weighting and reactive/static distinction passed\n";
    }
    std::cout<<"RHI temporal HDR resolve, jitter, object motion, disocclusion rejection, resize, enable/disable and scene history reset passed\n";
}
}
