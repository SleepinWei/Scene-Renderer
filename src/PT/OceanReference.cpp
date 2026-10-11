#include "PT/OceanReference.h"
#include "renderer/rhi/SceneAdapter.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <map>
namespace pt {
void captureOceanReference(const render::RenderWorldSnapshot& snapshot,const CpuScene& cpu,const Options& options,
                           std::shared_ptr<rhi::GraphicsDevice> device,const std::string& prefix){
    using namespace render;
    if(snapshot.frame.oceans.empty())throw std::invalid_argument("Ocean comparison requires an unfrozen ocean");
    auto world=snapshot;auto& f=world.frame;
    std::string aov="beauty";
    if(const char* value=std::getenv("SCENERENDERER_OCEAN_REFERENCE_AOV")){
        aov=value;const std::map<std::string,uint32_t> modes={{"beauty",0},{"body",6},{"reflection",7},{"sun",8},{"foam",9},{"border",10},{"downward-reflection",11}};
        const auto mode=modes.find(aov);
        if(mode==modes.end())throw std::invalid_argument("Unknown ocean reference AOV: "+aov);
        for(auto& ocean:f.oceans)ocean.opticalDebug=mode->second;
    }
    f.ssao=f.rsm=false;f.ddgi.enabled=false;f.weather.enabled=false;
    f.postProcess.enabled=false;f.temporalJitter={0,0};
    SceneAdapter adapter(device);auto resolved=adapter.resolve(world);
    const auto* auxiliary=resolved.auxiliaryPackets.empty()?nullptr:&resolved.auxiliaryPackets;
    ForwardPbrRenderer renderer(device,rhi::defaultShaderDirectory(),options.width,options.height,PbrPath::Scene);
    nlohmann::json report={{"width",options.width},{"height",options.height},{"time_seconds",f.timeSeconds},
        {"exposure",options.exposure},{"tone_mapper","1-exp(-radiance*exposure), gamma 2.2; same PT image exporter"},
        {"camera_m",{f.cameraPosition.x,f.cameraPosition.y,f.cameraPosition.z}},
        {"view_projection",std::vector<float>(glm::value_ptr(f.viewProjection),glm::value_ptr(f.viewProjection)+16)},
        {"sun_direction",{cpu.sunDirection.x,cpu.sunDirection.y,cpu.sunDirection.z}},
        {"sun_irradiance",{cpu.sunIrradiance.x,cpu.sunIrradiance.y,cpu.sunIrradiance.z}},{"sun_radius_radians",cpu.sunRadius},
        {"scope","Frozen-time comparison. Shared snapshot, camera, sea state and optics; raster transport/filtering and PT transport remain different. Synchronized renderer wall latency includes FFT, atmosphere/cache work and GPU waits, excludes scene resolution, presentation and image export."}};
    report["solar_reconstruction"]=std::getenv("SCENERENDERER_WATER_GLINT_BOX")?"4x4 box":"8x8 tent for smooth water; 4x4 tent for rough water";
    report["raster_aov"]=aov;
    report["raster_aov_scope"]="Above-water additive components, including foam and border weights. Only raster output is isolated; PT remains the full transport image. Sky outside water is unchanged.";
    report["solar_reconstruction_scope"]="Resolved near/middle water only; far slope-moment closure remains approximate";
    report["far_solar_model"]=std::getenv("SCENERENDERER_WATER_ANGULAR_MOMENTS")?"linearized angular Gaussian":"slope Gaussian with exact reflection Jacobian and 16 finite-sun samples";
    report["sky_reflection_model"]=std::getenv("SCENERENDERER_WATER_ANGULAR_MOMENTS")||std::getenv("SCENERENDERER_WATER_CENTER_SKY")?"centre normal":"4x4 spatial / Gaussian quadrature of Fresnel times sky radiance";
    report["reflection_integration_scope"]="Filtered physical smooth offshore water. Far solar switches above footprint 4 and blends through 4-8; rough, shoreline and opaque reflection hits retain prior paths. Gaussian slope closure omits water self-reflection.";
    for(const auto& o:f.oceans)report["water_surface_models"].push_back({{"physical",o.surfaceModel.physical},
        {"ior",WaterSurfaceModel::ior},{"roughness",o.surfaceModel.physical?o.surfaceModel.effectiveRoughness():0.f}});
    Image mask;mask.width=options.width;mask.height=options.height;
    for(uint32_t y=0;y<options.height;++y)for(uint32_t x=0;x<options.width;++x){
        glm::vec3 origin,direction;cpu.cameraRay((float(x)+.5f)/options.width,(float(y)+.5f)/options.height,origin,direction);
        Surface hit;const bool water=cpu.intersect(origin,direction,.0001f,1e8f,hit)&&hit.water;
        mask.radiance.emplace_back(water?1.f:0.f);
    }
    writeImage(mask,1,prefix+"-water-mask");
    report["mask_scope"]="PT first intersection at pixel centre, before stochastic AA; finite water mesh, not raster coverage or a supersampled silhouette";
    for(bool temporal:{false,true}){
        renderer.resetTemporal();resolved.frame.taa=temporal;
        std::vector<double> milliseconds;
        std::map<std::string,std::vector<double>> profile;
        uint64_t initialDispatches=0;size_t profileSamples=0;
        for(int i=0;i<20;++i){
            device->waitIdle();device->drainGpuProfile();
            const auto start=std::chrono::steady_clock::now();
            renderer.render(resolved.frame,resolved.packets,options.exposure,2.2f,auxiliary);
            device->waitIdle();
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            const auto samples=device->drainGpuProfile();
            if(i>=4){
                milliseconds.push_back(ms);profileSamples+=samples.size();
                for(const auto& sample:samples){
                    profile[sample.label].push_back(sample.endMilliseconds-sample.startMilliseconds);
                    if(sample.label.find("/initial")!=std::string::npos)++initialDispatches;
                }
            }
        }
        const auto rgba=renderer.readHDR();
        if(rgba.size()!=size_t(options.width)*options.height*4)throw std::runtime_error("Ocean comparison HDR dimensions differ");
        Image image;image.width=options.width;image.height=options.height;
        for(size_t i=0;i<rgba.size();i+=4)image.radiance.emplace_back(rgba[i],rgba[i+1],rgba[i+2]);
        const auto suffix=temporal?"-raster-taa":"-raster";
        writeImage(image,options.exposure,prefix+suffix);
        auto sorted=milliseconds;std::sort(sorted.begin(),sorted.end());
        report[temporal?"raster_taa":"raster"]={{"taa",temporal},{"frames",20},{"warmup_frames",4},
            {"wall_ms_samples",milliseconds},{"median_wall_ms",(sorted[7]+sorted[8])*.5},
            {"mean_wall_ms",std::accumulate(milliseconds.begin(),milliseconds.end(),0.)/milliseconds.size()}};
        auto& metrics=report[temporal?"raster_taa":"raster"];
        metrics["gpu_profile_samples"]=profileSamples;
        if(profileSamples)metrics["warm_initial_spectrum_dispatches"]=initialDispatches;
        metrics["gpu_stage_scope"]="Encoder counter intervals may overlap/include waits; only wall_ms_samples measure the complete synchronized renderer";
        for(auto& entry:profile){auto& times=entry.second;std::sort(times.begin(),times.end());
            metrics["stage_median_ms"][entry.first]=(times[(times.size()-1)/2]+times[times.size()/2])*.5;
        }
        std::cout<<"Ocean reference "<<suffix<<": "<<report[temporal?"raster_taa":"raster"]["median_wall_ms"]<<" ms median\n"<<std::flush;
    }
    std::ofstream out(prefix+"-comparison.json");out<<report.dump(2)<<'\n';
    if(!out)throw std::runtime_error("Cannot save ocean comparison metadata");
}
}
