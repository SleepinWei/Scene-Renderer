#include "PT/ProceduralCapture.h"
#include "renderer/rhi/GpuOcean.h"
#include "renderer/rhi/GpuGrass.h"
#include "renderer/rhi/GrassGeometry.h"
#include "rhi/ShaderAssets.h"
#include <iostream>
namespace pt {
render::RenderWorldSnapshot captureProcedural(const render::RenderWorldSnapshot &input,std::shared_ptr<rhi::GraphicsDevice> device,const CaptureOptions &options){
    validateCaptureOptions(options);auto result=input;
    if(input.terrain){const auto &t=*input.terrain;result.draws.push_back(freezeTerrain(t,options));
        if(options.grass&&t.source->grass){
            if(!device||!device->computeLimits().supported)throw std::invalid_argument("PT grass capture requires Metal/Vulkan compute; --pt-no-grass permits CPU terrain capture");
            auto settings=t.vegetation.value_or(t.source->vegetation);settings.capacity=std::min(settings.capacity,options.grassLimit);settings.validate();
            auto terrain=std::make_shared<render::GpuTerrain>(device,rhi::defaultShaderDirectory(),t.source->height,t.source->capacity,t.source->virtualColumns);
            // Fill visible height pages before freezing the native culled grass poses.
            for(int i=0;i<16;++i)terrain->update(input.frame,t.model);
            render::GpuGrass grass(device,rhi::defaultShaderDirectory(),terrain,t.model,settings.capacity,settings,t.source->waterMask,t.source->shorelineImages[3]);grass.update(t.model,input.frame.timeSeconds,settings);
            const auto count=grass.readArguments().instanceCount;auto poses=grass.readPoses(count);auto blades=render::grassBladeGeometry();auto mesh=std::make_shared<render::MeshPayload>();
            for(const auto &pose:poses){const uint32_t base=uint32_t(mesh->vertices.size());auto normal=glm::transpose(glm::inverse(glm::mat3(pose)));for(auto vertex:blades->vertices){vertex.position=glm::vec3(pose*glm::vec4(vertex.position,1));vertex.normal=glm::normalize(normal*vertex.normal);mesh->vertices.push_back(vertex);}for(auto index:blades->indices)mesh->indices.push_back(base+index);}
            if(count){render::SnapshotDraw draw;draw.objectId=t.source->id;draw.mesh=mesh;draw.parameters.albedoAlpha=glm::vec4(1);draw.parameters.factors={0,1,1,0};draw.extension.settings.w=1;draw.pathTracingKind=2;auto material=std::make_shared<render::MaterialPayload>();material->images[0]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,2,{90,123,65,255,16,43,23,255}});draw.material=material;result.draws.push_back(draw);}
            std::cout<<"PT frozen native grass: "<<count<<" clumps, capacity "<<settings.capacity<<'\n';
        }
        result.terrain.reset();
    }
    for(const auto &settings:input.frame.oceans){
        if(!device||!device->computeLimits().supported)throw std::invalid_argument("PT ocean capture requires Metal/Vulkan FFT compute");
        const float time=settings.animate?input.frame.timeSeconds*settings.timeScale:0;
        auto simulate=[&](const render::OceanSettings &s){render::GpuOcean ocean(device,rhi::defaultShaderDirectory(),s);ocean.simulate(time,s);return OceanSamples{s.size,ocean.readDisplacement(),ocean.readNormal(),ocean.readFoam()};};
        auto large=simulate(settings.spectrum);OceanSamples detail;if(settings.detailWaves){auto s=settings.spectrum;s.size=256;s.length=32;s.amplitude*=.06f*settings.detailStrength;s.seed+=71;s.windSpeed*=.6f;detail=simulate(s);}
        result.draws.push_back(freezeOcean(settings,large,detail,options));
    }
    result.frame.oceans.clear();return result;
}
}
