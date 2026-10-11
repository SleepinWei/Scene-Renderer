#include "PT/DdgiReference.h"
#include "PT/GpuPathTracer.h"
#include "renderer/rhi/DdgiPresets.h"
#include "renderer/rhi/SceneAdapter.h"
#include "rhi/ShaderAssets.h"
#include "engine/JobSystem.h"
#include <json/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace pt {
namespace {
nlohmann::json rgb(glm::vec3 v){return {v.x,v.y,v.z};}
Image image(const std::vector<float>& rgba,uint32_t width,uint32_t height){
    if(rgba.size()!=size_t(width)*height*4)throw std::runtime_error("DDGI reference HDR dimensions differ");
    Image result;result.width=width;result.height=height;
    for(size_t i=0;i<rgba.size();i+=4)result.radiance.emplace_back(rgba[i],rgba[i+1],rgba[i+2]);
    return result;
}
}
void captureDdgiReference(const render::RenderWorldSnapshot& snapshot,const CpuScene& cpu,const Options& options,
                          std::shared_ptr<rhi::GraphicsDevice> device,const std::string& prefix,
                          const std::string& scene,uint32_t frames,bool pathCapture,uint32_t pathSamples,uint32_t pathDepth,const CpuScene *probeWorld){
    using namespace render;
    auto world=snapshot;auto& frame=world.frame;
    frame.shadows=true;frame.ssao=frame.taa=frame.rsm=false;frame.postProcess={};frame.postProcess.enabled=false;
    frame.ddgi=ddgiScenePreset(scene);frame.forwardShading=false;
    frame.ddgi.pathTracedCapture=pathCapture;frame.ddgi.pathSamples=pathSamples;frame.ddgi.pathDepth=pathDepth;
    const uint32_t sweep=(frame.ddgi.probeCount()+frame.ddgi.probesPerFrame-1)/frame.ddgi.probesPerFrame;
    if(frames<sweep)throw std::invalid_argument("DDGI reference needs at least one complete probe sweep");
    SceneAdapter adapter(device);auto resolved=adapter.resolve(world);
    const auto* auxiliary=resolved.auxiliaryPackets.empty()?nullptr:&resolved.auxiliaryPackets;
    ForwardPbrRenderer renderer(device,rhi::defaultShaderDirectory(),options.width,options.height,PbrPath::Scene);
    std::shared_ptr<GpuProbeTracer> probeTracer;
    if(pathCapture){probeTracer=std::make_shared<GpuProbeTracer>(probeWorld?*probeWorld:cpu,device,frame.ddgi.captureResolution,pathSamples,pathDepth,std::make_shared<GpuSceneCache>(device),nullptr,options.gpuTraversal);renderer.setDdgiProbeTracer(probeTracer);}
    auto f=resolved.frame;f.ddgi.enabled=false;
    renderer.render(f,resolved.packets,options.exposure,2.2f,auxiliary);
    writeImage(image(renderer.readHDR(),options.width,options.height),options.exposure,prefix+"-raster-off");
    f.ddgi.enabled=true;
    nlohmann::json probeCaptures=nlohmann::json::array();
    device->waitIdle();const auto bakeStarted=std::chrono::steady_clock::now();
    for(uint32_t i=0;i<frames;++i){
        renderer.render(f,resolved.packets,options.exposure,2.2f,auxiliary);
        if(scene=="sponza"&&i>=frames-sweep){
            const auto stats=renderer.ddgiStats();
            const uint32_t last=(stats.cursor+frame.ddgi.probeCount()-1)%frame.ddgi.probeCount();
            if(last==295||last==423||last==663){
                const auto scratch=renderer.readDdgiCapture();
                const auto name=prefix+"-probe-capture-"+std::to_string(last)+".bin";
                std::ofstream raw(name,std::ios::binary);raw.write(reinterpret_cast<const char*>(scratch.data()),scratch.size()*sizeof(float));
                if(!raw)throw std::runtime_error("Cannot write DDGI raw probe capture");
                probeCaptures.push_back({{"index",last},{"frame",i+1},{"width",6*frame.ddgi.captureResolution},{"height",frame.ddgi.captureResolution}});
            }
        }
        // Bound transient resources in this offline loop; keep probe history intact.
        if(i%32==31)device->waitIdle();
        if(i%160==159)std::cout<<"DDGI reference: "<<i+1<<" / "<<frames<<" frames\n"<<std::flush;
    }
    device->waitIdle();const double bakeSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-bakeStarted).count();
    auto result=image(renderer.readHDR(),options.width,options.height);
    const auto positions=renderer.readGBuffer(0),normals=renderer.readGBuffer(1),albedos=renderer.readGBuffer(2);
    Image mask;mask.width=options.width;mask.height=options.height;
    for(size_t i=0;i<positions.size();i+=4){
        result.albedo.emplace_back(albedos[i],albedos[i+1],albedos[i+2]);
        result.normal.emplace_back(normals[i],normals[i+1],normals[i+2]);
        mask.radiance.emplace_back(positions[i+3]!=0?1.f:0.f);
    }
    writeImage(result,options.exposure,prefix+"-raster-ddgi");
    writeImage(mask,1,prefix+"-surface-mask");
    struct Query {uint32_t x,y;glm::vec3 p,n,geometric;};
    std::vector<Query> queries;std::vector<glm::vec4> queryPositions,queryNormals;
    for(uint32_t y=0;y<6;++y)for(uint32_t x=0;x<8;++x){
        const uint32_t px=(2*x+1)*options.width/16,py=(2*y+1)*options.height/12;
        const size_t at=(size_t(py)*options.width+px)*4;
        if(positions[at+3]==0)continue;
        glm::vec3 p(positions[at],positions[at+1],positions[at+2]),n(normals[at],normals[at+1],normals[at+2]);
        if(glm::dot(n,n)<.5f)continue;n=glm::normalize(n);
        Surface hit;
        if(!cpu.intersect(frame.cameraPosition,glm::normalize(p-frame.cameraPosition),.0001f,1000,hit)||glm::distance(hit.position,p)>.02f)continue;
        if(glm::dot(n,hit.geometricNormal)<.9f)continue; // Avoid strong bump-normal horizon differences.
        queries.push_back({px,py,p,n,hit.geometricNormal});
        // Diagnostic sampleDdgi uses N for its view bias. Compensate the input
        // so its biased position equals the surface pass's P + N*bias + V*bias.
        const auto V=glm::normalize(frame.cameraPosition-p);
        queryPositions.emplace_back(p+(V-n)*f.ddgi.viewBias,1);queryNormals.emplace_back(n,0);
    }
    if(queries.empty())throw std::runtime_error("DDGI reference has no aligned surface queries");
    const auto estimates=renderer.sampleDdgi(queryPositions,queryNormals);
    const auto field=renderer.readDdgiField();uint32_t initialized=0,active=0;
    for(uint32_t i=0;i<frame.ddgi.probeCount();++i){const auto state=field[size_t(i)*DdgiSettings::stride];initialized+=state.y>0;active+=state.x>.5f;}
    // Freeze the exact field: AA / direct-light ablations must not integrate a
    // different GI cache or inherit temporal colour from another variant.
    f.ddgi.freezeUpdates=true;
    // Dense surface queries use the same GPU query implementation and bias as
    // the actual composition pass. Keep this explicit offline readback out of render().
    std::vector<glm::vec4> densePositions,denseNormals;std::vector<size_t> densePixels;
    const auto materialEffects=renderer.readGBuffer(4),emissiveAO=renderer.readGBuffer(3);
    for(size_t i=0;i<positions.size();i+=4){
        const glm::vec3 p(positions[i],positions[i+1],positions[i+2]);
        const glm::vec3 n(normals[i],normals[i+1],normals[i+2]);
        if(positions[i+3]==0||materialEffects[i+3]<0||glm::dot(n,n)<.5f)continue;
        const auto N=glm::normalize(n),V=glm::normalize(f.cameraPosition-p);
        densePositions.emplace_back(p+(V-N)*f.ddgi.viewBias,1);denseNormals.emplace_back(N,0);densePixels.push_back(i/4);
    }
    const auto dense=renderer.sampleDdgi(densePositions,denseNormals);
    std::vector<glm::vec4> denseImage(size_t(options.width)*options.height,glm::vec4(0));
    for(size_t i=0;i<dense.size();++i)denseImage[densePixels[i]]=dense[i];
    auto dump=[&](const char* suffix,const void* bytes,size_t count){
        std::ofstream raw(prefix+suffix,std::ios::binary);raw.write(reinterpret_cast<const char*>(bytes),count);
        if(!raw)throw std::runtime_error("Cannot write DDGI surface intermediate");
    };
    dump("-surface-query.bin",denseImage.data(),denseImage.size()*sizeof(glm::vec4));
    dump("-surface-position.bin",positions.data(),positions.size()*sizeof(float));
    dump("-surface-normal.bin",normals.data(),normals.size()*sizeof(float));
    dump("-surface-albedo.bin",albedos.data(),albedos.size()*sizeof(float));
    dump("-surface-emissive-ao.bin",emissiveAO.data(),emissiveAO.size()*sizeof(float));
    auto capture=[&](const std::string& name,uint32_t count){
        renderer.resetTemporal();
        for(uint32_t i=0;i<count;++i){renderer.render(f,resolved.packets,options.exposure,2.2f,auxiliary);if(i%32==31)device->waitIdle();}
        writeImage(image(renderer.readHDR(),options.width,options.height),options.exposure,prefix+name);
    };
    f.taa=true;capture("-raster-ddgi-taa",64);
    f.taaHighQuality=false;capture("-raster-ddgi-taa-legacy",64);f.taaHighQuality=true;
    f.skySpecularScale=0;capture("-raster-ddgi-taa-no-sky-specular",64);
    f.taa=false;capture("-raster-ddgi-no-sky-specular",1);
    f.skySpecularScale=1;f.shadows=false;capture("-raster-ddgi-no-shadow",1);
    f.shadows=true;
    const auto baseFrame=f;const auto projection=f.viewProjection*glm::inverse(f.view);
    for(bool quality:{false,true}){
        f=baseFrame;f.taa=true;f.taaHighQuality=quality;renderer.resetTemporal();
        for(uint32_t i=0;i<80;++i){
            const float offset=i<16?0.f:float(i-15)*.0125f;
            const glm::vec3 delta(0,0,offset);f.cameraPosition=baseFrame.cameraPosition+delta;
            f.view=baseFrame.view*glm::translate(glm::mat4(1),-delta);f.viewProjection=projection*f.view;
            renderer.render(f,resolved.packets,options.exposure,2.2f,auxiliary);if(i%32==31)device->waitIdle();
        }
        writeImage(image(renderer.readHDR(),options.width,options.height),options.exposure,prefix+(quality?"-motion-taa":"-motion-taa-legacy"));
    }
    f=baseFrame;f.taa=false;f.cameraPosition+=glm::vec3(0,0,.8f);
    f.view=baseFrame.view*glm::translate(glm::mat4(1),glm::vec3(0,0,-.8f));f.viewProjection=projection*f.view;
    capture("-motion-no-taa",1);f=baseFrame;
    const auto frozenField=renderer.readDdgiField();
    if(field!=frozenField)throw std::runtime_error("DDGI diagnostic ablations changed the frozen probe field");
    // Save the actual native cache, not an inferred active/inactive layout.
    {
        std::ofstream data(prefix+"-probe-field.bin",std::ios::binary);
        data.write(reinterpret_cast<const char*>(field.data()),field.size()*sizeof(glm::vec4));
        if(!data)throw std::runtime_error("Cannot write DDGI probe cache");
    }
    f=baseFrame;f.taa=true;renderer.resetTemporal();
    for(uint32_t i=0;i<32;++i)renderer.render(f,resolved.packets,options.exposure,2.2f,auxiliary);
    auto probeCapture=[&](const char* name,int mode,bool xray,int layer){
        f.ddgi.debugProbes=mode;f.ddgi.debugXray=xray;f.ddgi.debugLayer=layer;
        renderer.render(f,resolved.packets,options.exposure,2.2f,auxiliary);
        const auto rgba=renderer.readOutput();
        std::ofstream out(prefix+name,std::ios::binary);out<<"P6\n"<<options.width<<' '<<options.height<<"\n255\n";
        for(size_t i=0;i<rgba.size();i+=4)out.write(reinterpret_cast<const char*>(rgba.data()+i),3);
        if(!out)throw std::runtime_error("Cannot write DDGI probe visualization");
    };
    probeCapture("-probes-state.ppm",2,false,-1);
    probeCapture("-probes-xray.ppm",2,true,-1);
    probeCapture("-probes-layer.ppm",2,true,1);
    probeCapture("-probes-irradiance.ppm",1,false,-1);
    if(field!=renderer.readDdgiField())throw std::runtime_error("DDGI visualization changed the frozen probe cache");
    f=baseFrame;
    nlohmann::json report={{"scene",scene},{"width",options.width},{"height",options.height},{"time",frame.timeSeconds},
        {"camera",rgb(frame.cameraPosition)},{"exposure",options.exposure},{"gamma",2.2},{"raster_frames",frames},
        {"probe_sweep_frames",sweep},{"initialized_probes",initialized},{"active_probes",active},{"probe_count",frame.ddgi.probeCount()},
        {"probe_cache_bytes",frame.ddgi.cacheBytes()},{"raster_ambient",frame.ambient},{"ao",false},{"taa",false},{"rsm",false},
        {"probe_captures",probeCaptures},{"surface_query_pixels",dense.size()},
        {"probe_volume",{{"origin",rgb(frame.ddgi.origin)},{"counts",{frame.ddgi.counts.x,frame.ddgi.counts.y,frame.ddgi.counts.z}},
            {"spacing",frame.ddgi.spacing},{"stride_vec4",DdgiSettings::stride},{"capture_resolution",frame.ddgi.captureResolution},
            {"max_distance",frame.ddgi.maxDistance},{"intensity",frame.ddgi.intensity},
            {"debug_radius",f.ddgi.debugRadius},{"layer_capture_index",1},{"layer_capture_y",frame.ddgi.origin.y+frame.ddgi.spacing}}},
        {"surface_diagnostics",{{"taa_frames",64},{"taa_high_quality",true},{"legacy_taa_comparison",true},{"motion_frames",64},{"motion_warmup",16},{"motion_camera_delta",{0,0,.8}},{"material_mips",true},{"material_anisotropy_requested",8},{"frozen_probe_field_unchanged",true},{"sky_specular_ablation_scale",0},{"raw_shadow_ablation",true}}},
        {"sun_direction",rgb(cpu.sunDirection)},{"sun_irradiance",rgb(cpu.sunIrradiance)},{"sun_radius",cpu.sunRadius},
        {"reference_method","E/pi from cosine hemisphere path integration at aligned GBuffer surfaces. First-segment miss uses sky only, excluding analytic direct sun already handled by surface direct lighting. Secondary surface paths retain sun NEE and all BSDF lobes. Same shading normal; geometric epsilon offset; no fitted intensity or exposure."},
        {"probe_bake_wall_seconds",bakeSeconds},{"probe_bake_timing_scope","whole raster frames including probe updates, explicit GPU completion; not a hardware GPU timestamp"},{"probe_capture_backend",pathCapture?"shared-gpu-pt":"raster-lambert-feedback"},{"probe_path_samples",pathCapture?pathSamples:0},{"probe_path_depth",pathCapture?pathDepth:0},
        {"probe_native_rt",probeTracer?probeTracer->nativeRayTracing():false},{"probe_world_lod",probeWorld!=nullptr},
        {"point_samples",2048},{"point_depth",options.maxDepth},{"points",nlohmann::json::array()}};
    engine::JobSystem jobs(8);std::vector<std::future<nlohmann::json>> work;
    for(size_t i=0;i<queries.size();++i)work.push_back(jobs.submit([&,i]{
        const auto& q=queries[i];const auto axis=std::abs(q.n.y)<.9f?glm::vec3(0,1,0):glm::vec3(1,0,0);
        const auto T=glm::normalize(glm::cross(axis,q.n)),B=glm::cross(q.n,T);
        glm::dvec3 sum(0),squares(0);uint64_t rays=0;
        for(uint32_t s=0;s<2048;++s){
            Random random=Random::forPixel(options.seed,uint32_t(i),s,false);
            const float u=random.uniform(),phi=6.28318530718f*random.uniform();
            const auto direction=T*(std::sqrt(u)*std::cos(phi))+B*(std::sqrt(u)*std::sin(phi))+q.n*std::sqrt(1-u);
            glm::vec3 value(0);const auto origin=q.p+q.geometric*.0002f;
            if(glm::dot(direction,q.geometric)>0){
                Surface hit;
                if(cpu.intersect(origin,direction,.0001f,std::numeric_limits<float>::infinity(),hit))
                    value=cpu.trace(origin,direction,random,options.maxDepth,rays);
                else if(cpu.environment)value=cpu.environment->evaluate(direction);
            }
            if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z))throw std::runtime_error("Nonfinite DDGI point reference");
            const glm::dvec3 v(value);sum+=v;squares+=v*v;
        }
        const auto mean=sum/2048.;const auto variance=glm::max(squares/2048.-mean*mean,glm::dvec3(0));
        const auto error=glm::sqrt(variance/2047.);
        return nlohmann::json{{"pixel",{q.x,q.y}},{"position",rgb(q.p)},{"normal",rgb(q.n)},
            {"ddgi",rgb(glm::vec3(estimates[i]))},{"coverage",estimates[i].w},{"pt_E_over_pi",rgb(glm::vec3(mean))},
            {"pt_standard_error",rgb(glm::vec3(error))},{"secondary_rays",rays}};
    }));
    for(auto& future:work)report["points"].push_back(future.get());
    std::ofstream output(prefix+"-ddgi-reference.json");output<<report.dump(2)<<'\n';
    if(!output)throw std::runtime_error("Cannot write DDGI reference report");
    std::cout<<"DDGI reference: "<<queries.size()<<" cosine-hemisphere PT queries saved\n"<<std::flush;
}
}
