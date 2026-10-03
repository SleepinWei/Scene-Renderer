#include "renderer/rhi/GpuMaterial.h"
#include "renderer/rhi/GpuVirtualTexture.h"
#include "renderer/rhi/GpuImageCache.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace render {
static_assert(sizeof(MaterialExtension) == 80, "Material extension std140 ABI");
static_assert(sizeof(MaterialParameters) == 48 && offsetof(MaterialParameters, factors) == 16 &&
                  offsetof(MaterialParameters, emissiveNormal) == 32,
              "Material std140 ABI changed");
namespace {
void validate(const MaterialParameters &p) {
    for (const auto &v : {p.albedoAlpha, p.factors, p.emissiveNormal})
        for (int i = 0; i < 4; ++i)
            if (!std::isfinite(v[i]) || v[i] < 0)
                throw std::invalid_argument("Renderer: invalid material parameters");
    if (p.albedoAlpha.w > 1 || p.factors.x > 1 || p.factors.y > 1 || p.factors.z > 1 || p.factors.w > 1)
        throw std::invalid_argument("Renderer: material scalar exceeds normalized range");
}
std::vector<std::shared_ptr<const ImageRGBA8>> materialImages(const MaterialDesc &desc) {
    static const auto defaults = [] {
        std::array<std::shared_ptr<const ImageRGBA8>, 6> images;
        const std::array<std::array<uint8_t, 4>, 6> bytes{{{255, 255, 255, 255},
                                                           {128, 128, 255, 255},
                                                           {255, 255, 255, 255},
                                                           {255, 255, 255, 255},
                                                           {255, 255, 255, 255},
                                                           {255, 255, 0, 255}}};
        for (size_t i = 0; i < 6; ++i)
            images[i] =
                std::make_shared<const ImageRGBA8>(ImageRGBA8{1, 1, {bytes[i].begin(), bytes[i].end()}});
        return images;
    }();
    std::vector<std::shared_ptr<const ImageRGBA8>> result;
    for (size_t i = 0; i < 6; ++i) {
        const auto &shared = i == 5 ? desc.sharedSpecial : desc.sharedImages[i];
        const auto &value = i == 5 ? desc.special : desc.images[i];
        if (shared)
            result.push_back(shared);
        else if (!value.width && !value.height && value.pixels.empty())
            result.push_back(defaults[i]);
        else
            result.push_back(std::make_shared<const ImageRGBA8>(value));
    }
    return result;
}

} // namespace
rhi::BindingLayout GpuMaterial::layout() {
    using namespace rhi;
    return {1,
            {{0, BindingType::UniformBuffer, ShaderStage::Fragment, "MaterialData", 48},
             {1, BindingType::SampledTexture, ShaderStage::Fragment, "albedoMap", 0},
             {2, BindingType::SampledTexture, ShaderStage::Fragment, "normalMap", 0},
             {3, BindingType::SampledTexture, ShaderStage::Fragment, "metallicMap", 0},
             {4, BindingType::SampledTexture, ShaderStage::Fragment, "roughnessMap", 0},
             {5, BindingType::SampledTexture, ShaderStage::Fragment, "aoMap", 0},
             {6, BindingType::UniformBuffer, ShaderStage::Fragment, "MaterialExtension", 80},
             {7, BindingType::SampledTexture, ShaderStage::Fragment, "specialMap", 0}}};
}
GpuMaterial::GpuMaterial(std::shared_ptr<rhi::GraphicsDevice> device, const MaterialDesc &desc,
                         std::shared_ptr<GpuVirtualTexture> vt)
    : virtualTexture_(std::move(vt)), resources_(std::move(device)) {
    if (virtualTexture_ && (virtualTexture_->heightField() || virtualTexture_->layers() != 5))
        throw std::invalid_argument("Material VT needs five material planes");
    validate(desc.parameters);
    parameters_ = resources_.buffer({sizeof(MaterialParameters),
                                     rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDestination,
                                     "PBR material parameters"},
                                    &desc.parameters);
    auto sampler = resources_.sampler({desc.filter, rhi::AddressMode::Repeat});
    std::vector<rhi::BindingEntry> entries{{0, parameters_, 0, sizeof(MaterialParameters), {}, {}}};
    imageCache_ = GpuImageCache::forDevice(resources_.device);
    shoreline_=bool(desc.shorelineImages[0]);
    if(shoreline_) {
        if(!virtualTexture_)throw std::invalid_argument("Shoreline requires terrain VT");
        for(size_t i=0;i<4;++i) {
            auto& image=desc.shorelineImages[i];
            if(!image)throw std::invalid_argument("Shoreline requires four images");
            if(i<3 && (image->height!=image->width*2 || !image->width || (image->width&(image->width-1))))
                throw std::invalid_argument("Invalid shoreline mip atlas");
            images_.push_back(imageCache_->acquire(image));
        }
    }
    if (!virtualTexture_) {
        for (const auto &source : materialImages(desc))
            images_.push_back(imageCache_->acquire(source));
    }
    for (unsigned i = 0; i < 5; ++i) {
        if(virtualTexture_ && shoreline_ && i>0) {
            const size_t maps[]={0,1,0,2,3};
            auto sampling=i==4?resources_.sampler({rhi::Filter::Linear,rhi::AddressMode::ClampToEdge}):sampler;
            entries.push_back({i+1,{},0,0,images_[maps[i]]->view(),sampling});
        } else if (virtualTexture_)
            entries.push_back({i + 1, {}, 0, 0, virtualTexture_->atlas(i), virtualTexture_->sampler()});
        else
            entries.push_back({i + 1, {}, 0, 0, images_[i]->view(), sampler});
    }
    parameterData_ = desc.parameters;
    transparent_ = desc.transparent;
    extension_ = resources_.buffer(
        {80, rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDestination, "Material extended lobes"});
    updateExtension(desc.extension);
    entries.push_back({6, extension_, 0, 80, {}, {}});
    if (virtualTexture_)
        entries.push_back({7, {}, 0, 0, virtualTexture_->pageTable(), virtualTexture_->sampler()});
    else
        entries.push_back({7, {}, 0, 0, images_[5]->view(), sampler});
    bindings_ = resources_.bindings({layout(), entries});
    shadowBindings_ = resources_.bindings({shadowLayout(), {entries[0], entries[1], entries[6], entries[7]}});
    rsmBindings_ = resources_.bindings(
        {rsmLayout(), {entries[0], entries[1], entries[6], entries[7], entries[2], entries[3], entries[4], entries[5]}});
}
size_t GpuMaterial::imageUploadBytes(std::shared_ptr<rhi::GraphicsDevice> device, const MaterialDesc &desc) {
    auto cache=GpuImageCache::forDevice(std::move(device));
    if(desc.shorelineImages[0])return cache->missingBytes({desc.shorelineImages.begin(),desc.shorelineImages.end()});
    return cache->missingBytes(materialImages(desc));
}
rhi::BindingLayout GpuMaterial::shadowLayout() {
    using namespace rhi;
    return {1,
            {{0, BindingType::UniformBuffer, ShaderStage::Fragment, "MaterialData", 48},
             {1, BindingType::SampledTexture, ShaderStage::Fragment, "albedoMap", 0},
             {6, BindingType::UniformBuffer, ShaderStage::Fragment, "MaterialExtension", 80},
             {7, BindingType::SampledTexture, ShaderStage::Fragment, "specialMap", 0}}};
}
rhi::BindingLayout GpuMaterial::rsmLayout() {
    auto layout = shadowLayout();
    layout.entries.push_back(
        {2, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, "normalMap", 0});
    layout.entries.push_back(
        {3, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, "metallicMap", 0});
    layout.entries.push_back({4, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment,"roughnessMap",0});
    layout.entries.push_back({5, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment,"aoMap",0});
    return layout;
}
void GpuMaterial::bindRsm(rhi::CommandList &list) const { list.bindBindingSet(rsmBindings_); }
void GpuMaterial::bindShadow(rhi::CommandList &list) const { list.bindBindingSet(shadowBindings_); }
void GpuMaterial::update(const MaterialParameters &p) {
    validate(p);
    parameterData_ = p;
    resources_.device->writeBuffer(parameters_, 0, sizeof(p), &p);
}
void GpuMaterial::updateExtension(const MaterialExtension &e) {
    for (const auto &v : {e.lobes, e.settings,e.shoreHeight,e.shoreSurface})
        for (int i = 0; i < 4; ++i)
            if (!std::isfinite(v[i]))
                throw std::invalid_argument("Material nonfinite extended parameter");
    if (e.lobes.x < 0 || e.lobes.x > 1 || e.lobes.y < 0 || e.lobes.y > 1 || std::abs(e.lobes.z) > .95f ||
        e.lobes.w < 0 || e.settings.x < 0)
        throw std::invalid_argument("Material extended lobe outside domain");
    if(shoreline_ && (e.shoreHeight.y<=0 || e.shoreHeight.z<0 || e.shoreHeight.w<=0 ||
        e.shoreSurface.x<=0 || e.shoreSurface.y<0 || e.shoreSurface.z>1 ||
        e.shoreSurface.y>=e.shoreSurface.z || e.shoreSurface.w<0))
        throw std::invalid_argument("Invalid shoreline material parameters");
    extensionData_ = e;
    extensionData_.features = {(virtualTexture_ ? int(MaterialFeature::VirtualTexture) : 0) | (shoreline_ ? int(MaterialFeature::Shoreline) : 0), 0, 0, 0};
    resources_.device->writeBuffer(extension_, 0, 80, &extensionData_);
}
void GpuMaterial::bind(rhi::CommandList &list) const { list.bindBindingSet(bindings_); }
} // namespace render
