#include "renderer/rhi/GpuMaterial.h"
#include "renderer/rhi/GpuVirtualTexture.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace render {
static_assert(sizeof(MaterialExtension)==48,"Material extension std140 ABI");
static_assert(sizeof(MaterialParameters) == 48 && offsetof(MaterialParameters, factors) == 16 && offsetof(MaterialParameters, emissiveNormal) == 32, "Material std140 ABI changed");
namespace {
void validate(const MaterialParameters& p) {
    for (const auto& v : {p.albedoAlpha, p.factors, p.emissiveNormal}) for (int i = 0; i < 4; ++i)
        if (!std::isfinite(v[i]) || v[i] < 0) throw std::invalid_argument("Renderer: invalid material parameters");
    if (p.albedoAlpha.w > 1 || p.factors.x > 1 || p.factors.y > 1 || p.factors.z > 1 || p.factors.w > 1)
        throw std::invalid_argument("Renderer: material scalar exceeds normalized range");
}
}
rhi::BindingLayout GpuMaterial::layout() {
    using namespace rhi;
    return {1, {{0, BindingType::UniformBuffer, ShaderStage::Fragment, "MaterialData", 48},
        {1, BindingType::SampledTexture, ShaderStage::Fragment, "albedoMap", 0},
        {2, BindingType::SampledTexture, ShaderStage::Fragment, "normalMap", 0},
        {3, BindingType::SampledTexture, ShaderStage::Fragment, "metallicMap", 0},
        {4, BindingType::SampledTexture, ShaderStage::Fragment, "roughnessMap", 0},
        {5, BindingType::SampledTexture, ShaderStage::Fragment, "aoMap", 0},
        {6,BindingType::UniformBuffer,ShaderStage::Fragment,"MaterialExtension",48},{7,BindingType::SampledTexture,ShaderStage::Fragment,"specialMap",0}}};
}
GpuMaterial::GpuMaterial(std::shared_ptr<rhi::GraphicsDevice> device, const MaterialDesc& desc,std::shared_ptr<GpuVirtualTexture> vt) : virtualTexture_(std::move(vt)),resources_(std::move(device)) {
    if(virtualTexture_ && (virtualTexture_->heightField() || virtualTexture_->layers()!=5))throw std::invalid_argument("Material VT needs five material planes");
    validate(desc.parameters);
    parameters_ = resources_.buffer({sizeof(MaterialParameters), rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDestination, "PBR material parameters"}, &desc.parameters);
    auto sampler = resources_.sampler({desc.filter, rhi::AddressMode::Repeat});
    std::vector<rhi::BindingEntry> entries{{0, parameters_, 0, sizeof(MaterialParameters), {}, {}}};
    const std::array<std::array<uint8_t, 4>, 5> defaults{{{255,255,255,255}, {128,128,255,255}, {255,255,255,255}, {255,255,255,255}, {255,255,255,255}}};
    for (unsigned i = 0; i < 5; ++i) {
        if(virtualTexture_){entries.push_back({i+1,{},0,0,virtualTexture_->atlas(i),virtualTexture_->sampler()});continue;}
        const auto& image = desc.images[i];const bool missing = !image.width && !image.height && image.pixels.empty();
        if (!missing && (!image.width || !image.height || image.pixels.size() != uint64_t(image.width) * image.height * 4)) throw std::invalid_argument("Renderer: invalid RGBA8 image");
        auto texture = resources_.texture({missing ? 1u : image.width, missing ? 1u : image.height, rhi::Format::RGBA8UNorm,
            rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination, "PBR map " + std::to_string(i)});
        resources_.device->writeTexture(texture, missing ? defaults[i].data() : image.pixels.data(), missing ? 4 : image.pixels.size());
        entries.push_back({i + 1, {}, 0, 0, resources_.view(texture), sampler});
    }
    parameterData_=desc.parameters;transparent_=desc.transparent;extension_=resources_.buffer({48,rhi::BufferUsage::Uniform|rhi::BufferUsage::CopyDestination,"Material extended lobes"});updateExtension(desc.extension);
    entries.push_back({6,extension_,0,48,{},{}});
    if(virtualTexture_)entries.push_back({7,{},0,0,virtualTexture_->pageTable(),virtualTexture_->sampler()});
    else {
    const auto& special=desc.special;const bool missing=special.pixels.empty()&&!special.width&&!special.height;if(!missing && (!special.width || !special.height || special.pixels.size()!=uint64_t(special.width)*special.height*4))throw std::invalid_argument("Material invalid packed special map");auto texture=resources_.texture({missing?1u:special.width,missing?1u:special.height,rhi::Format::RGBA8UNorm,rhi::TextureUsage::Sampled|rhi::TextureUsage::CopyDestination,"Coat/anisotropy/height/thickness map"});const uint8_t fallback[]={255,255,0,255};resources_.device->writeTexture(texture,missing?fallback:special.pixels.data(),missing?4:special.pixels.size());
entries.push_back({7,{},0,0,resources_.view(texture),sampler});
    }
    bindings_ = resources_.bindings({layout(), entries});
    shadowBindings_=resources_.bindings({shadowLayout(),{entries[0],entries[1],entries[6],entries[7]}});
    rsmBindings_=resources_.bindings({rsmLayout(),{entries[0],entries[1],entries[6],entries[7],entries[2],entries[3]}});
}
rhi::BindingLayout GpuMaterial::shadowLayout() {
    using namespace rhi;return {1,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"MaterialData",48},{1,BindingType::SampledTexture,ShaderStage::Fragment,"albedoMap",0},{6,BindingType::UniformBuffer,ShaderStage::Fragment,"MaterialExtension",48},{7,BindingType::SampledTexture,ShaderStage::Fragment,"specialMap",0}}};
}
rhi::BindingLayout GpuMaterial::rsmLayout(){auto layout=shadowLayout();layout.entries.push_back({2,rhi::BindingType::SampledTexture,rhi::ShaderStage::Fragment,"normalMap",0});layout.entries.push_back({3,rhi::BindingType::SampledTexture,rhi::ShaderStage::Fragment,"metallicMap",0});return layout;}
void GpuMaterial::bindRsm(rhi::CommandList& list) const {list.bindBindingSet(rsmBindings_);}
void GpuMaterial::bindShadow(rhi::CommandList& list) const {list.bindBindingSet(shadowBindings_);}
void GpuMaterial::update(const MaterialParameters& p) { validate(p);parameterData_=p;resources_.device->writeBuffer(parameters_, 0, sizeof(p), &p); }
void GpuMaterial::updateExtension(const MaterialExtension& e){for(const auto& v:{e.lobes,e.settings})for(int i=0;i<4;++i)if(!std::isfinite(v[i]))throw std::invalid_argument("Material nonfinite extended parameter");if(e.lobes.x<0 || e.lobes.x>1 || e.lobes.y<0 || e.lobes.y>1 || std::abs(e.lobes.z)>.95f || e.lobes.w<0 || e.settings.x<0)throw std::invalid_argument("Material extended lobe outside domain");extensionData_=e;extensionData_.features={virtualTexture_?int(MaterialFeature::VirtualTexture):0,0,0,0};resources_.device->writeBuffer(extension_,0,48,&extensionData_);}
void GpuMaterial::bind(rhi::CommandList& list) const { list.bindBindingSet(bindings_); }
}
