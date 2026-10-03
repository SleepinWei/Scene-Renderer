#include "renderer/rhi/SceneAdapter.h"
#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/GpuImageCache.h"
#include "renderer/rhi/GpuGrass.h"
#include "renderer/rhi/GpuSubdivision.h"
#include "rhi/ShaderAssets.h"
#include "system/InputManager.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <map>
#include <set>
#include <limits>
namespace render {
struct SceneAdapter::Cache {
    struct MeshRecord {
        std::shared_ptr<const MeshPayload> source;
        std::shared_ptr<GpuMesh> gpu;
    };
    struct MaterialRecord {
        std::shared_ptr<const MaterialPayload> source;
        std::shared_ptr<GpuMaterial> gpu;
    };
    std::map<uint64_t, MeshRecord> meshes;
    using MaterialKey = std::pair<uint64_t, uint32_t>;
    std::map<MaterialKey, MaterialRecord> materials;
    struct SubdivisionRecord {
        std::shared_ptr<const MeshPayload> source;
        std::shared_ptr<const MaterialPayload> material;
        std::shared_ptr<GpuSubdivision> gpu;
    };
    std::map<std::pair<uint64_t, uint64_t>, SubdivisionRecord> subdivisions;
    struct TerrainRecord {
        std::shared_ptr<const TerrainPayload> source;
        std::shared_ptr<GpuTerrain> gpu;
        std::shared_ptr<GpuVirtualTexture> virtualMaterial;
        std::shared_ptr<GpuGrass> grass;
        std::shared_ptr<GpuMaterial> material, grassMaterial;
        uint64_t epoch = 0;
    };
    std::unique_ptr<TerrainRecord> terrain;
    uint64_t terrainEpoch = 0, uploadEpoch = 0;
    std::shared_ptr<GpuImageCache> images;
    std::shared_ptr<GpuMaterial> fallback;
};
SceneAdapter::SceneAdapter(std::shared_ptr<rhi::GraphicsDevice> device)
    : device_(std::move(device)), cache_(std::make_unique<Cache>()) {
    cache_->images = GpuImageCache::forDevice(device_);
    MaterialDesc fallback;
    fallback.parameters.factors = {0, .5f, 1, 0};
    fallback.parameters.emissiveNormal.w = 0;
    cache_->fallback = std::make_shared<GpuMaterial>(device_, fallback);
}
SceneAdapter::~SceneAdapter() = default;
void SceneAdapter::invalidateAssets() {
    cache_->meshes.clear();
    cache_->materials.clear();
    cache_->subdivisions.clear();
    cache_->terrain.reset();
    builder_.invalidateAssets();
}
SceneFrame SceneAdapter::collect(const std::shared_ptr<RenderScene> &scene, float time) {
    auto input = InputManager::GetInstance();
    auto snapshot =
        builder_.capture(scene, time >= 0 ? time : float(glfwGetTime()), uint32_t(std::max(1, input->width)),
                         uint32_t(std::max(1, input->height)));
    return resolve(*snapshot);
}
SceneFrame SceneAdapter::resolve(const RenderWorldSnapshot &snapshot) {
    SceneFrame result;
    result.frame = snapshot.frame;
    result.exposure = snapshot.exposure;
    auto admit = [&](size_t bytes) {
        if (snapshot.asynchronousStreaming && result.assetUploads &&
            (result.assetUploads >= 2 || result.uploadBytes + bytes > 32 * 1024 * 1024)) {
            ++result.assetsPending;
            return false;
        }
        result.uploadBytes += bytes;
        ++result.assetUploads;
        ++cache_->uploadEpoch;
        return true;
    };
    result.frame.taa = result.frame.taa && device_->computeLimits().maxStorageImages > 0;
    if (snapshot.terrain) {
        const auto &terrain = *snapshot.terrain;
        const auto &source = terrain.source;
        if (!device_->computeLimits().maxStorageImages)
            throw std::invalid_argument("Terrain requires storage compute");
        if (!cache_->terrain || cache_->terrain->source != source) {
            admit(size_t(source->capacity) * 64 * (4 * sizeof(MeshVertex) + 6 * 4) + 12 * 1024 * 1024);
            auto record = std::make_unique<Cache::TerrainRecord>();
            record->source = source;
            record->epoch = ++cache_->terrainEpoch;
            record->gpu = std::make_shared<GpuTerrain>(device_, rhi::defaultShaderDirectory(), source->height,
                                                       source->capacity);
            if (snapshot.asynchronousStreaming)
                record->gpu->heightTexture()->enableAsync();
            record->virtualMaterial = std::make_shared<GpuVirtualTexture>(device_, source->material);
            if (snapshot.asynchronousStreaming)
                record->virtualMaterial->enableAsync();
            MaterialDesc material;
            material.parameters = terrain.parameters;
            record->material = std::make_shared<GpuMaterial>(device_, material, record->virtualMaterial);
            if (source->grass) {
                record->grass = std::make_shared<GpuGrass>(device_, rhi::defaultShaderDirectory(),
                                                           record->gpu, terrain.model);
                MaterialDesc grass;
                grass.parameters.factors = {0, 1, 1, 0};
                grass.parameters.emissiveNormal.w = 0;
                grass.extension.settings.w = 1;
                grass.images[0] = {1, 2, {90, 123, 65, 255, 16, 43, 23, 255}};
                record->grassMaterial = std::make_shared<GpuMaterial>(device_, grass);
            }
            cache_->terrain = std::move(record);
        }
        auto &record = *cache_->terrain;
        record.gpu->update(result.frame, terrain.model);
        record.virtualMaterial->prepare(result.frame.viewProjection, terrain.model,
                                        result.frame.viewportWidth, result.frame.viewportHeight, true);
        record.material->update(terrain.parameters);
        result.frame.historyKey ^= record.gpu->heightTexture()->version() * 0x9e3779b97f4a7c15ull ^
                                   record.virtualMaterial->version() ^ (record.epoch * 0xd1b54a32d192ed03ull);
        result.packets.push_back({record.gpu->mesh(), record.material, terrain.model, 0, terrain.wireframe});
        if (record.grass) {
            record.grass->update(terrain.model, result.frame.timeSeconds);
            result.packets.push_back({record.grass->mesh(), record.grassMaterial, glm::mat4(1)});
        }
    } else
        cache_->terrain.reset();
    std::set<uint64_t> usedMeshes;
    std::set<Cache::MaterialKey> usedMaterials;
    std::set<std::pair<uint64_t, uint64_t>> usedSubdivisions;
    for (const auto &draw : snapshot.draws) {
        // Mark every referenced record before admission. A pending mesh/material
        // must not evict an already-uploaded dependent asset and starve forever.
        if (draw.material)
            usedMaterials.insert({draw.material->id, draw.shading});
        if (draw.subdivision && draw.mesh)
            usedSubdivisions.insert({draw.objectId, draw.mesh->id});
        const auto &source = draw.mesh;
        if (!source)
            throw std::invalid_argument("Snapshot contains pending mesh");
        usedMeshes.insert(source->id);
        auto &mesh = cache_->meshes[source->id];
        if (mesh.source != source) {
            if (!admit(source->vertices.size() * sizeof(MeshVertex) + source->indices.size() * 4))
                continue;
            auto uploaded = std::make_shared<GpuMesh>(device_, source->vertices, source->indices);
            mesh.source = source;
            mesh.gpu = std::move(uploaded);
        }
        auto material = cache_->fallback;
        if (draw.material) {
            const auto key = Cache::MaterialKey{draw.material->id, draw.shading};
            usedMaterials.insert(key);
            auto &cached = cache_->materials[key];
            if (cached.source != draw.material) {
                MaterialDesc desc;
                desc.parameters = draw.parameters;
                desc.extension = draw.extension;
                desc.sharedImages = draw.material->images;
                desc.sharedSpecial = draw.material->special;
                if (!admit(GpuMaterial::imageUploadBytes(device_, desc)))
                    continue;
                auto uploaded = std::make_shared<GpuMaterial>(device_, desc);
                cached.source = draw.material;
                cached.gpu = std::move(uploaded);
            }
            material = cached.gpu;
            material->update(draw.parameters);
            material->updateExtension(draw.extension);
        }
        auto gpu = mesh.gpu;
        if (draw.subdivision) {
            auto key = std::make_pair(draw.objectId, source->id);
            usedSubdivisions.insert(key);
            auto &cached = cache_->subdivisions[key];
            if (cached.source != source || cached.material != draw.material) {
                cached.source = source;
                cached.material = draw.material;
                cached.gpu = std::make_shared<GpuSubdivision>(
                    device_, rhi::defaultShaderDirectory(), source->vertices, source->indices,
                    draw.material && draw.material->height ? *draw.material->height : ImageRGBA8{});
            }
            float distance = std::numeric_limits<float>::max();
            for (const auto &vertex : source->vertices)
                distance = std::min(distance, glm::length(glm::vec3(result.frame.view * draw.model *
                                                                    glm::vec4(vertex.position, 1))));
            const uint32_t level =
                uint32_t(std::ceil(glm::mix(10.f, 1.f, glm::clamp((distance - .2f) / .8f, 0.f, 1.f))));
            cached.gpu->update(level, material->extension().settings.y);
            gpu = cached.gpu->mesh();
        }
        result.packets.push_back(
            {gpu, material, draw.model, draw.objectId * 0x9e3779b97f4a7c15ull ^ source->id, draw.wireframe});
    }
    for (auto it = cache_->meshes.begin(); it != cache_->meshes.end();)
        if (!usedMeshes.count(it->first))
            it = cache_->meshes.erase(it);
        else
            ++it;
    for (auto it = cache_->materials.begin(); it != cache_->materials.end();)
        if (!usedMaterials.count(it->first))
            it = cache_->materials.erase(it);
        else
            ++it;
    for (auto it = cache_->subdivisions.begin(); it != cache_->subdivisions.end();)
        if (!usedSubdivisions.count(it->first))
            it = cache_->subdivisions.erase(it);
        else
            ++it;
    cache_->images->trim();
    result.gpuImages = cache_->images->stats();
    result.frame.historyKey ^= cache_->uploadEpoch * 0x94d049bb133111ebull;
    return result;
}
} // namespace render
