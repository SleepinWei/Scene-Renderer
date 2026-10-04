#include "renderer/rhi/GalleryDiagnostics.h"
#include "renderer/rhi/ShadowRenderer.h"
#include <json/json.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <stdexcept>
namespace render {
namespace {
template<class T> void writeRaw(const std::filesystem::path& path,const std::vector<T>& values) {
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(values.data()),std::streamsize(values.size()*sizeof(T)));
    if (!out) throw std::runtime_error("Cannot save diagnostic buffer: "+path.string());
}
nlohmann::json matrix(const glm::mat4& m) {
    nlohmann::json rows=nlohmann::json::array();
    for(int y=0;y<4;++y)rows.push_back({m[0][y],m[1][y],m[2][y],m[3][y]});
    return rows;
}
}
void exportGalleryDiagnostics(const std::string& directory,const std::string& name,
    std::shared_ptr<rhi::GraphicsDevice> device,const SceneAdapter& adapter,
    ForwardPbrRenderer& renderer,const FrameData& frame) {
    const std::filesystem::path dir(directory);
    nlohmann::json j={{"scene",name},{"width",frame.viewportWidth},{"height",frame.viewportHeight},
        {"capture_frames",64},{"time_seconds",frame.timeSeconds},
        {"backend",rhi::requestedBackend()==rhi::Backend::Metal?"Metal":"Vulkan"},
        {"camera_view",matrix(frame.view)},{"raw_format","native little-endian float32 / uint8, row zero at top"}};
    auto positions=renderer.readGBuffer(0);
    if(positions.size()!=size_t(frame.viewportWidth)*frame.viewportHeight*4)
        throw std::runtime_error("Diagnostic G-buffer dimensions differ from frame");
    for(float value:positions)if(!std::isfinite(value))throw std::runtime_error("Nonfinite diagnostic G-buffer");
    writeRaw(dir/(name+"-positions.f32"),positions);
    const auto shadow=renderer.shadowParameters();auto depth=renderer.readShadowDepth();
    const auto extent=uint32_t(std::sqrt(double(depth.size())));
    if(size_t(extent)*extent!=depth.size())throw std::runtime_error("Diagnostic shadow atlas is not square");
    writeRaw(dir/(name+"-shadow.f32"),depth);
    j["shadow"]={{"extent",extent},{"camera_near",shadow.cascades.x},{"distance",shadow.cascades.y},
        {"blend_fraction",shadow.cascades.z},{"fade_fraction",shadow.cascades.w},
        {"angular_radius",shadow.filter.y},{"pcss",shadow.filter.x>.5f},{"lights",nlohmann::json::array()}};
    for(size_t l=0;l<shadow.lights.size();++l) {
        auto info=shadow.lights[l];if(!info.y)continue;
        nlohmann::json light={{"type",info.z},{"base",info.x},{"count",info.y},
            {"splits",{shadow.splits[l].x,shadow.splits[l].y,shadow.splits[l].z,shadow.splits[l].w}},
            {"tiles",nlohmann::json::array()}};
        for(int t=0;t<info.y;++t){const auto rect=shadow.rects[info.x+t];light["tiles"].push_back({{"rect",{rect.x,rect.y,rect.z,rect.w}},{"matrix",matrix(shadow.matrices[info.x+t])}});}
        j["shadow"]["lights"].push_back(light);
    }
    auto terrain=adapter.terrainVirtualTextures();
    if(terrain.height && terrain.material) {
        j["terrain_model"]=matrix(terrain.model);
        for(const auto& pair:{std::make_pair("height",terrain.height),std::make_pair("material",terrain.material)}) {
            const auto& vt=*pair.second;std::string prefix=name+"-vt-"+pair.first;
            auto table=device->readTextureFloat(vt.tableTexture());
            const uint32_t tableWidth=vt.extent()/GpuVirtualTexture::Tile;
            writeRaw(dir/(prefix+"-table.f32"),table);
            nlohmann::json data={{"extent",vt.extent()},{"max_mip",vt.maxMip()},{"capacity",vt.capacity()},
                {"resident",vt.residentPages()},{"pending",vt.pendingPages()},{"physical_bytes",vt.physicalBytes()},
                {"feedback_pages",vt.feedbackPageCount()},{"feedback_samples",vt.feedbackSamples()},
                {"table_width",tableWidth},{"table_height",table.size()/(tableWidth*4)},
                {"minimum",vt.minimum()},{"maximum",vt.maximum()},{"layers",vt.layers()},
                {"tile",GpuVirtualTexture::Tile},{"border",GpuVirtualTexture::Border},{"pitch",GpuVirtualTexture::Pitch}};
            data["atlas_extent"]=uint32_t(std::sqrt(double(vt.capacity())))*GpuVirtualTexture::Pitch;
            for(uint32_t layer=0;layer<vt.layers();++layer) {
                if(vt.heightField())writeRaw(dir/(prefix+"-atlas-"+std::to_string(layer)+".f32"),device->readTextureFloat(vt.atlasTexture(layer)));
                else writeRaw(dir/(prefix+"-atlas-"+std::to_string(layer)+".u8"),device->readTexture(vt.atlasTexture(layer)));
            }
            j["vt"][pair.first]=data;
        }
    }
    std::ofstream out(dir/(name+"-diagnostics.json"));out<<j.dump(2)<<'\n';
    if(!out)throw std::runtime_error("Cannot save diagnostic metadata");
}
}
