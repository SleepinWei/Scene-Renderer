#include "renderer/rhi/HiZVisualization.h"
#include "renderer/rhi/RasterVisibility.h"
#include "rhi/Device.h"
// Also used by the standalone Vulkan validation target without an importer.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <json/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <set>
namespace render {
void exportHiZVisualization(ForwardPbrRenderer& renderer,const FrameData& source,const std::vector<DrawPacket>& input,const std::string& directory,const std::string& name,float exposure){
    const auto dir=std::filesystem::path(directory);std::filesystem::create_directories(dir);
    auto frame=source;frame.taa=false;frame.triangleCulling=false;
    // Match the camera-frustum candidate list in both captures. Keep the full
    // source geometry available for shadow and water auxiliary views.
    std::vector<DrawPacket> packets;for(const auto& packet:input)if(!frame.objectCulling || rasterPacketVisible(packet,frame))packets.push_back(packet);
    frame.objectCulling=false;
    auto png=[&](const std::string& suffix,const std::vector<uint8_t>& pixels){
        if(pixels.size()!=size_t(frame.viewportWidth)*frame.viewportHeight*4 || !stbi_write_png((dir/(name+suffix+".png")).string().c_str(),frame.viewportWidth,frame.viewportHeight,4,pixels.data(),frame.viewportWidth*4))throw std::runtime_error("Cannot save HiZ capture image");
    };
    frame.hizCulling=false;renderer.resetTemporal();renderer.render(frame,packets,exposure,2.2f,&input);
    const auto baseline=renderer.readOutput();const auto baseHdr=renderer.readHDR();png("-off",baseline);
    frame.hizCulling=true;renderer.resetTemporal();renderer.render(frame,packets,exposure,2.2f,&input);
    const auto active=renderer.readOutput();const auto hdr=renderer.readHDR();png("-on",active);
    const auto stats=renderer.hizCullStats();if(!stats.active)throw std::runtime_error("HiZ capture has no active occluder/candidate pair");
    float error=0;for(size_t i=0;i<hdr.size();++i)error=std::max(error,std::abs(hdr[i]-baseHdr[i]));
    if(!std::isfinite(error) || error>.002f || baseline!=active)throw std::runtime_error("HiZ visualization baseline differs from active output");
    auto counts=renderer.readHiZCullCounts();auto bounds=renderer.hizCullBounds();std::set<size_t> occluders;
    nlohmann::json metadata={{"scene",name},{"backend",rhi::requestedBackend()==rhi::Backend::Metal?"Metal":"Vulkan"},
        {"width",frame.viewportWidth},{"height",frame.viewportHeight},{"display_equal",baseline==active},{"hdr_max_error",error},
        {"source","actual GPU depth mip readbacks and indexed indirect arguments"},{"depth_convention","standard 0..1, farthest-depth max reduction, clear 1"},
        {"prepass_triangles",stats.prepassTriangles},{"objects",nlohmann::json::array()},{"mips",nlohmann::json::array()}};
    size_t culled=0;uint64_t before=0,after=0;
    for(const auto& count:counts){
        auto box=std::find_if(bounds.begin(),bounds.end(),[&](const auto& b){return b.packet==count.packet;});
        if(box==bounds.end())throw std::runtime_error("HiZ diagnostic bounds missing");
        if(count.occluder)occluders.insert(count.packet);
        before+=count.inputTriangles;after+=count.outputTriangles;culled+=count.inputTriangles>0 && count.outputTriangles==0;
        metadata["objects"].push_back({{"packet",count.packet},{"id",packets[count.packet].id},{"rectangle",{box->rectangle.x,box->rectangle.y,box->rectangle.z,box->rectangle.w}},
            {"nearest_depth",box->nearestDepth},{"testable",box->testable},{"occluder",count.occluder},{"input_triangles",count.inputTriangles},{"output_triangles",count.outputTriangles}});
    }
    metadata["candidate_objects"]=counts.size();metadata["culled_objects"]=culled;metadata["nonzero_draws_before"]=packets.size();metadata["nonzero_draws_after"]=packets.size()-culled;
    metadata["main_triangles_before"]=before;metadata["main_triangles_after"]=after;
    for(uint32_t mip=0;mip<stats.pyramidLevels;++mip){
        auto data=renderer.readHiZDepth(mip);const auto width=std::max(frame.viewportWidth>>mip,1u),height=std::max(frame.viewportHeight>>mip,1u);
        if(data.size()!=size_t(width)*height)throw std::runtime_error("HiZ capture mip size differs");
        for(float depth:data)if(!std::isfinite(depth) || depth<0 || depth>1)throw std::runtime_error("HiZ capture contains invalid depth");
        const auto file=name+"-mip-"+std::to_string(mip)+".f32";std::ofstream raw(dir/file,std::ios::binary);raw.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()*4));
        if(!raw)throw std::runtime_error("Cannot save HiZ GPU mip");
        metadata["mips"].push_back({{"level",mip},{"width",width},{"height",height},{"file",file}});
    }
    // Reference only: remove the selected occluders to reveal hidden objects.
    // This image is not the baseline and does not represent final rendering.
    std::vector<DrawPacket> revealed;for(size_t i=0;i<packets.size();++i)if(!occluders.count(i))revealed.push_back(packets[i]);
    frame.hizCulling=false;frame.shadows=false;frame.ssao=false;frame.rsm=false;renderer.resetTemporal();renderer.render(frame,revealed,exposure);png("-occluders-removed",renderer.readOutput());
    std::ofstream out(dir/(name+"-capture.json"));out<<metadata.dump(2)<<'\n';if(!out)throw std::runtime_error("Cannot save HiZ metadata");
}
}
