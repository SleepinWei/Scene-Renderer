#include "renderer/rhi/RasterVisibility.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/GpuImageCache.h"
#include "renderer/rhi/GpuGrass.h"
#include "renderer/rhi/GpuVegetation.h"
#include "renderer/rhi/TreeGeometry.h"
#include "renderer/rhi/GpuTreeWind.h"
#include "renderer/rhi/GpuSubdivision.h"
#include "renderer/rhi/ClusterGeometry.h"
#include "engine/JobSystem.h"
#include <iostream>
#include "rhi/ShaderAssets.h"
#include "system/InputManager.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <map>
#include <set>
#include <limits>
#include <chrono>
#include <cmath>
#include <cstring>
#include <tuple>
namespace render {
struct SceneAdapter::Cache {
    struct MeshRecord {
        std::shared_ptr<const MeshPayload> source;
        std::shared_ptr<GpuMesh> gpu;
        std::shared_ptr<GpuTreeWind> wind;
        std::shared_ptr<const MeshPayload> windSource;
        std::shared_ptr<const MeshPayload> uploading;
        std::shared_ptr<GpuMesh> upload;
        uint32_t vertexCursor = 0, indexCursor = 0, tangentCursor = 0;
        std::shared_ptr<const MeshPayload> clusterSource;
        std::shared_future<std::shared_ptr<const ClusterHierarchy>> clusterBuild;
        std::shared_ptr<GpuClusterGeometry> clusters;
    };
    struct MaterialRecord {
        std::shared_ptr<const MaterialPayload> requested;
        bool used = false;
        std::shared_ptr<const MaterialPayload> source;
        std::shared_ptr<GpuMaterial> gpu;
        MaterialParameters parameters;
        MaterialExtension extension;
        std::shared_ptr<const MaterialPayload> uploading;
        std::vector<std::shared_ptr<GpuImage>> imageUploads;
    };
    std::map<uint64_t, MeshRecord> meshes;
    using MaterialKey = std::tuple<uint64_t, uint32_t, uint64_t>;
    std::map<MaterialKey, std::vector<MaterialRecord>> materials;
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
        std::shared_ptr<GpuVegetation> vegetation;
        std::shared_ptr<GpuMaterial> material, grassMaterial;
        uint64_t epoch = 0;
        MaterialParameters parameters;
        glm::mat4 feedbackModel{1};
    };
    std::unique_ptr<TerrainRecord> terrain;
    uint64_t terrainEpoch = 0, uploadEpoch = 0;
    std::shared_ptr<GpuImageCache> images;
    std::shared_ptr<GpuMaterial> fallback;
    Cache() = default;
    Cache(const Cache &other)
        : meshes(other.meshes), materials(other.materials), subdivisions(other.subdivisions),
          terrain(other.terrain ? std::make_unique<TerrainRecord>(*other.terrain) : nullptr),
          terrainEpoch(other.terrainEpoch), uploadEpoch(other.uploadEpoch), images(other.images),
          fallback(other.fallback) {}
};
namespace {
bool same(const MaterialParameters &a, const MaterialParameters &b) {
    return a.albedoAlpha == b.albedoAlpha && a.factors == b.factors && a.emissiveNormal == b.emissiveNormal;
}
bool same(const MaterialExtension &a, const MaterialExtension &b) {
    return a.lobes == b.lobes && a.settings == b.settings && a.features == b.features &&
        a.shoreHeight == b.shoreHeight && a.shoreSurface == b.shoreSurface && a.specularShape == b.specularShape && a.rainWetCells == b.rainWetCells;
}
uint64_t materialVariantHash(const MaterialParameters &p, const MaterialExtension &e) {
    uint64_t hash = 14695981039346656037ull;
    auto word = [&](uint32_t value) { hash = (hash ^ value) * 1099511628211ull; };
    // Hash explicit fields, never struct padding. Exact comparisons below still
    // decide identity; signed zeros must hash together because they compare equal.
    for (const auto &v : {p.albedoAlpha, p.factors, p.emissiveNormal, e.lobes, e.settings,
                          e.shoreHeight, e.shoreSurface, e.specularShape})
        for (int i = 0; i < 4; ++i) { uint32_t bits = 0; if (v[i] != 0) std::memcpy(&bits, &v[i], sizeof(bits)); word(bits); }
    for (int i = 0; i < 4; ++i) word(uint32_t(e.features[i]));
    for (const auto &v : e.rainWetCells) for (int i = 0; i < 4; ++i) word(v[i]);
    return hash;
}
}
SceneAdapter::SceneAdapter(std::shared_ptr<rhi::GraphicsDevice> device)
    : device_(std::move(device)), cache_(std::make_unique<Cache>()) {
    cache_->images = GpuImageCache::forDevice(device_);
    MaterialDesc fallback;
    fallback.parameters.factors = {0, .5f, 1, 0};
    fallback.parameters.emissiveNormal.w = 0;
    cache_->fallback = std::make_shared<GpuMaterial>(device_, fallback);
}
SceneAdapter::~SceneAdapter() = default;
TerrainVirtualTextures SceneAdapter::terrainVirtualTextures() const {
    device_->checkThread();
    if (!cache_->terrain) return {};
    const auto& terrain = *cache_->terrain;
    return {terrain.gpu->heightTexture(), terrain.virtualMaterial, terrain.feedbackModel};
}
void SceneAdapter::setMeshUploadBudget(MeshUploadBudget budget) {
    device_->checkThread();
    if(previous_)throw std::logic_error("Cannot configure uploads during publication");
    if(budget.bytesPerFrame < sizeof(MeshVertex) || budget.bytesPerChunk < sizeof(MeshVertex) ||
       !std::isfinite(budget.cpuMilliseconds) || budget.cpuMilliseconds <= 0 || !budget.maxPendingMeshes)
        throw std::invalid_argument("Invalid mesh upload budget");
    meshUploadBudget_ = budget;
}
void SceneAdapter::beginPublication() {
    device_->checkThread();
    if (previous_) throw std::logic_error("Scene publication already active");
    auto candidate = std::make_unique<Cache>(*cache_);
    previous_ = std::move(cache_);
    cache_ = std::move(candidate);
}
void SceneAdapter::commitPublication() {
    device_->checkThread();
    if (!previous_) throw std::logic_error("No scene publication active");
    previous_.reset();
}
void SceneAdapter::rollbackPublication() {
    device_->checkThread();
    if (!previous_) return;
    cache_.swap(previous_);
    previous_.reset();
    auxiliaryBatches_.batch({});
    batches_.batch({}); // Drop leases derived from a rejected candidate.
    cache_->images->releaseIdle(); // Discard unleased images from partially built materials.
}
void SceneAdapter::invalidateAssets() {
    device_->checkThread();
    if (previous_) throw std::logic_error("Cannot invalidate during publication");
    cache_->meshes.clear();
    cache_->materials.clear();
    cache_->subdivisions.clear();
    cache_->terrain.reset();
    batches_.batch({});auxiliaryBatches_.batch({});
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
    device_->checkThread();
    const bool local = !previous_;
    if (local) beginPublication();
    try {
        auto result = resolveCandidate(snapshot);
        if (local) commitPublication();
        return result;
    } catch (...) {
        if (local) rollbackPublication();
        throw;
    }
}
SceneFrame SceneAdapter::resolveCandidate(const RenderWorldSnapshot &snapshot) {
    SceneFrame result;
    result.frame = snapshot.frame;
    auto enabled=[](const char* name){auto value=std::getenv(name);return value&&std::strcmp(value,"1")==0;};
    result.frame.clusters.enabled=(result.frame.clusters.enabled||enabled("SCENERENDERER_ENABLE_CLUSTER_GEOMETRY"))&&!enabled("SCENERENDERER_DISABLE_CLUSTER_GEOMETRY");
    result.exposure = snapshot.exposure;
    const auto uploadStarted = std::chrono::steady_clock::now();
    std::set<uint64_t> pendingMeshIds;
    auto elapsed = [&] {
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStarted).count();
    };
    auto admit = [&](size_t bytes, bool published = true) {
        if (snapshot.asynchronousStreaming && result.assetUploads &&
            (result.assetUploads >= 2 || result.uploadBytes + bytes > 32 * 1024 * 1024)) {
            ++result.assetsPending;
            return false;
        }
        result.uploadBytes += bytes;
        ++result.assetUploads;
        if(published)++cache_->uploadEpoch;
        return true;
    };
    result.frame.taa = result.frame.taa && device_->computeLimits().maxStorageImages > 0;
    if (snapshot.terrain) {
        const auto &terrain = *snapshot.terrain;
        const auto &source = terrain.source;
        if (!device_->computeLimits().maxStorageImages)
            throw std::invalid_argument("Terrain requires storage compute");
        if (!cache_->terrain || cache_->terrain->source != source) {
            size_t shoreBytes=0;for(const auto& image:source->shorelineImages)if(image)shoreBytes+=image->pixels.size();
            admit(shoreBytes+size_t(source->capacity) * 64 * (4 * sizeof(MeshVertex) + 6 * 4) + 12 * 1024 * 1024 +
                  (source->grass ? ([&]{size_t bytes=source->vegetation.species.empty()?size_t(source->vegetation.capacity)*128:0;for(auto& v:source->vegetation.species){bytes+=size_t(v.capacity)*128*(source->vegetation.speciesLod?2:1)*v.variants;
                      if(uint32_t(v.type)>=5){for(uint32_t variant=0;variant<v.variants;variant++)for(uint32_t lod=0;lod<(source->vegetation.speciesLod?2u:1u);lod++){auto g=treeGeometry(v.type,lod,v.seed+variant*104729u);for(auto mesh:{g.bark,g.foliage})bytes+=mesh->vertices.size()*(sizeof(MeshVertex)*3+sizeof(TreeWindVertex))+mesh->indices.size()*4;}}else bytes+=256*1024;
                  }return bytes;}()) : 0) +
                  (source->waterMask ? source->waterMask->pixels.size() : 0));
            auto record = std::make_unique<Cache::TerrainRecord>();
            record->source = source;
            record->epoch = ++cache_->terrainEpoch;
            record->gpu = std::make_shared<GpuTerrain>(device_, rhi::defaultShaderDirectory(), source->height,
                                                       source->capacity,source->virtualColumns);
            if (snapshot.asynchronousStreaming)
                record->gpu->heightTexture()->enableAsync();
            auto materialSource=source->material;materialSource.minimum=source->height.minimum;materialSource.maximum=source->height.maximum;
            record->virtualMaterial = std::make_shared<GpuVirtualTexture>(device_, std::move(materialSource),source->virtualColumns);
            if (snapshot.asynchronousStreaming)
                record->virtualMaterial->enableAsync();
            MaterialDesc material;
            material.parameters = terrain.parameters;
            material.extension = terrain.extension;material.shorelineImages=source->shorelineImages;
            record->material = std::make_shared<GpuMaterial>(device_, material, record->virtualMaterial);
            record->parameters = terrain.parameters;
            if(source->grass&&!source->vegetation.species.empty())record->vegetation=std::make_shared<GpuVegetation>(device_,rhi::defaultShaderDirectory(),record->gpu,terrain.model,source->vegetation,source->waterMask,source->shorelineImages[3]);
            if (source->grass&&source->vegetation.species.empty()) {
                record->grass = std::make_shared<GpuGrass>(device_, rhi::defaultShaderDirectory(),
                                                           record->gpu, terrain.model, source->vegetation.capacity, source->vegetation, source->waterMask, source->shorelineImages[3]);
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
        record.feedbackModel=terrain.model;
        record.gpu->update(result.frame, terrain.model);
        record.virtualMaterial->prepare(result.frame.viewProjection, terrain.model,
                                        result.frame.viewportWidth, result.frame.viewportHeight, true);
        if (!same(record.parameters, terrain.parameters)) {
            // Keep candidate parameter buffers separate for publication rollback,
            // while reusing terrain VT and the immutable shoreline image leases.
            MaterialDesc desc;desc.parameters=terrain.parameters;
            desc.extension=terrain.extension;desc.shorelineImages=source->shorelineImages;
            record.material=std::make_shared<GpuMaterial>(device_,desc,record.virtualMaterial);
            record.parameters = terrain.parameters;
        }
        result.frame.historyKey ^= record.gpu->heightTexture()->version() * 0x9e3779b97f4a7c15ull ^
                                   record.virtualMaterial->version() ^ (record.epoch * 0xd1b54a32d192ed03ull);
        record.material->updateExtension(terrain.extension);
        result.packets.push_back({record.gpu->mesh(), record.material, terrain.model, record.epoch*0xd1b54a32d192ed03ull, terrain.wireframe,record.gpu->geometryStable()});
        if(record.vegetation){record.vegetation->update(terrain.model,result.frame.timeSeconds,terrain.vegetation.value_or(source->vegetation));record.vegetation->append(result.packets);}
        if (record.grass) {
            record.grass->update(terrain.model, result.frame.timeSeconds, terrain.vegetation.value_or(source->vegetation));
            result.packets.push_back({record.grass->mesh(), record.grassMaterial, glm::mat4(1),0,false,record.grass->historyReliable()});
        }
    } else
        cache_->terrain.reset();
    std::set<uint64_t> usedMeshes;
    std::set<uint64_t> animatedMeshes;
    auto windCommands=device_->createCommandList();bool recordedWind=false;
    for (auto &[key, variants] : cache_->materials)
        for (auto &variant : variants) variant.used = false;
    std::set<std::pair<uint64_t, uint64_t>> usedSubdivisions;
    for (const auto &draw : snapshot.draws) {
        auto materialExtension=draw.extension;
        if(materialExtension.specularShape.z==0 && draw.pathTracingIor==0 && (draw.pathTracingBsdfModel==0 || draw.pathTracingBsdfModel==2) &&
           (draw.pathTracingSpecularAnisotropy!=0 || draw.pathTracingSpecularRotation!=0))
            materialExtension.specularShape={draw.pathTracingSpecularAnisotropy,draw.pathTracingSpecularRotation,draw.pathTracingBsdfModel==2?2.f:1.f,0};
        Cache::MaterialRecord *materialRecord = nullptr;
        // Mark every referenced record before admission. A pending mesh/material
        // must not evict an already-uploaded dependent asset and starve forever.
        if (draw.material) {
            auto &variants = cache_->materials[{draw.material->id, draw.shading, materialVariantHash(draw.parameters, materialExtension)}];
            auto match = std::find_if(variants.begin(), variants.end(), [&](const auto &record) {
                return record.requested == draw.material && same(record.parameters, draw.parameters) &&
                       same(record.extension, materialExtension);
            });
            if (match == variants.end()) {
                Cache::MaterialRecord variant;
                // A new image revision may stream over several frames. Keep a
                // matching published material visible without mutating its lease.
                auto fallback = std::find_if(variants.begin(), variants.end(), [&](const auto &record) {
                    return record.gpu && same(record.parameters, draw.parameters) &&
                           same(record.extension, materialExtension);
                });
                if (fallback != variants.end()) { variant.source = fallback->source; variant.gpu = fallback->gpu; }
                variant.requested = draw.material;
                variant.parameters = draw.parameters;
                variant.extension = materialExtension;
                variants.push_back(std::move(variant));
                match = std::prev(variants.end());
            }
            materialRecord = &*match;
            materialRecord->used = true;
        }
        if (draw.subdivision && draw.mesh)
            usedSubdivisions.insert({draw.objectId, draw.mesh->id});
        const auto &source = draw.mesh;
        if (!source)
            throw std::invalid_argument("Snapshot contains pending mesh");
        usedMeshes.insert(source->id);
        auto &mesh = cache_->meshes[source->id];
        if(mesh.source == source && mesh.uploading) {
            mesh.uploading.reset();mesh.upload.reset();mesh.vertexCursor=mesh.indexCursor=mesh.tangentCursor=0;
        }
        if (mesh.source != source) {
            if(!source->pathTracingTangents.empty() && source->pathTracingTangents.size()!=source->vertices.size())
                throw std::invalid_argument("Raster material tangent count mismatch");
            if(!snapshot.asynchronousStreaming) {
                admit(source->vertices.size() * sizeof(MeshVertex) + source->indices.size() * 4 + source->pathTracingTangents.size()*16);
                auto uploaded = std::make_shared<GpuMesh>(device_, source->vertices, source->indices);
                uploaded->setMaterialTangents(source->pathTracingTangents);
                mesh.source = source;
                mesh.gpu = std::move(uploaded);
                mesh.uploading.reset();mesh.upload.reset();mesh.vertexCursor=mesh.indexCursor=mesh.tangentCursor=0;
            } else {
                if(mesh.uploading != source) {
                    const auto pending=std::count_if(cache_->meshes.begin(),cache_->meshes.end(),
                        [](const auto& record){return bool(record.second.upload);});
                    if(!mesh.upload && pending>=meshUploadBudget_.maxPendingMeshes) {
                        ++result.assetsPending;pendingMeshIds.insert(source->id);continue;
                    }
                    if(!admit(0,false)) {pendingMeshIds.insert(source->id);continue;}
                    if(source->vertices.size() > UINT32_MAX || source->indices.size() > UINT32_MAX)
                        throw std::invalid_argument("Mesh upload exceeds uint32 capacity");
                    auto uploaded = GpuMesh::beginUpload(device_, uint32_t(source->vertices.size()),
                                                        uint32_t(source->indices.size()), !source->pathTracingTangents.empty());
                    mesh.uploading = source;
                    mesh.upload = std::move(uploaded);
                    mesh.vertexCursor = mesh.indexCursor = mesh.tangentCursor = 0;
                }
                while(result.meshUploadBytes < meshUploadBudget_.bytesPerFrame) {
                    if(result.meshUploadChunks && elapsed() >= meshUploadBudget_.cpuMilliseconds)break;
                    const size_t bytes = std::min(meshUploadBudget_.bytesPerChunk,
                                                  meshUploadBudget_.bytesPerFrame-result.meshUploadBytes);
                    size_t uploaded = 0;
                    if(mesh.vertexCursor < source->vertices.size()) {
                        const auto count = uint32_t(std::min(bytes/sizeof(MeshVertex),source->vertices.size()-mesh.vertexCursor));
                        if(!count)break;
                        mesh.upload->uploadVertices(mesh.vertexCursor,source->vertices.data()+mesh.vertexCursor,count);
                        mesh.vertexCursor += count;
                        uploaded = size_t(count)*sizeof(MeshVertex);
                    } else if(mesh.indexCursor < source->indices.size()) {
                        const auto count = uint32_t(std::min(bytes/4,source->indices.size()-mesh.indexCursor));
                        if(!count)break;
                        mesh.upload->uploadIndices(mesh.indexCursor,source->indices.data()+mesh.indexCursor,count);
                        mesh.indexCursor += count;
                        uploaded = size_t(count)*4;
                    } else if(device_->computeLimits().supported && mesh.tangentCursor<source->pathTracingTangents.size()){
                        const auto count=uint32_t(std::min(bytes/16,source->pathTracingTangents.size()-mesh.tangentCursor));
                        if(!count)break;
                        mesh.upload->uploadMaterialTangents(mesh.tangentCursor,source->pathTracingTangents.data()+mesh.tangentCursor,count);
                        mesh.tangentCursor+=count;uploaded=size_t(count)*16;
                    } else break;
                    result.meshUploadBytes += uploaded;
                    result.uploadBytes += uploaded;
                    ++result.meshUploadChunks;
                }
                if(mesh.vertexCursor == source->vertices.size() && mesh.indexCursor == source->indices.size() &&
                   (!device_->computeLimits().supported || mesh.tangentCursor==source->pathTracingTangents.size())) {
                    mesh.source = source;
                    mesh.gpu = std::move(mesh.upload);
                    mesh.uploading.reset();mesh.vertexCursor=mesh.indexCursor=mesh.tangentCursor=0;
                    ++cache_->uploadEpoch;
                    pendingMeshIds.erase(source->id);
                } else {
                    ++result.assetsPending;
                    pendingMeshIds.insert(source->id);
                    // Keep an already-published base mesh visible during revision upload.
                    if(!mesh.gpu || draw.subdivision)continue;
                }
            }
        }
        auto material = cache_->fallback;
        if (draw.material) {
            auto &cached = *materialRecord;
            const bool imagesChanged = cached.source != draw.material;
            if(!imagesChanged && cached.uploading) {
                cached.uploading.reset();cached.imageUploads.clear();
            }
            if (imagesChanged) {
                MaterialDesc desc;
                desc.parameters = draw.parameters;
                desc.extension = materialExtension;
                desc.sharedImages = draw.material->images;
                desc.sharedSpecial = draw.material->special;
                bool finishMaterial=true;
                if(imagesChanged && snapshot.asynchronousStreaming) {
                    if(cached.uploading!=draw.material) {
                        if(!admit(0,false))continue;
                        cached.uploading=draw.material;cached.imageUploads.clear();
                    }
                    std::vector<GpuImageSource> sources;
                    for(size_t i=0;i<desc.sharedImages.size();++i)if(desc.sharedImages[i])sources.push_back({desc.sharedImages[i],materialMipPolicy(i,desc.parameters)});
                    if(desc.sharedSpecial)sources.push_back({desc.sharedSpecial,materialMipPolicy(5,desc.parameters)});
                    bool ready=true;
                    for(size_t index=0;index<sources.size();++index) {
                        if(index==cached.imageUploads.size()) {
                            auto gpu=cache_->images->acquire(sources[index].image,true,sources[index].policy);
                            if(!gpu){ready=false;break;}
                            cached.imageUploads.push_back(std::move(gpu));
                        }
                        auto& image=cached.imageUploads[index];
                        while(!image->ready() && result.imageUploadBytes<8*1024*1024) {
                            if(result.imageUploadChunks && elapsed()>=meshUploadBudget_.cpuMilliseconds)break;
                            const auto bytes=cache_->images->upload(image,std::min(size_t(256*1024),size_t(8*1024*1024-result.imageUploadBytes)));
                            if(!bytes)break;
                            result.imageUploadBytes+=bytes;result.uploadBytes+=bytes;++result.imageUploadChunks;
                        }
                        if(!image->ready()){ready=false;break;}
                    }
                    if(!ready){++result.assetsPending;if(!cached.gpu)continue;finishMaterial=false;}
                    else ++cache_->uploadEpoch;
                } else if (imagesChanged && !admit(GpuMaterial::imageUploadBytes(device_, desc)))continue;
                if(finishMaterial){
                auto uploaded = std::make_shared<GpuMaterial>(device_, desc);
                cached.source = draw.material;
                cached.gpu = std::move(uploaded);
                cached.parameters = draw.parameters;
                cached.extension = materialExtension;
                cached.uploading.reset();cached.imageUploads.clear();
                ++result.rasterMaterials.builds;
                result.rasterMaterials.parameterUploadBytes += sizeof(MaterialParameters) + sizeof(MaterialExtension);
                }
            } else ++result.rasterMaterials.cacheHits;
            material = cached.gpu;
        }
        auto gpu = mesh.gpu;
        if(device_->computeLimits().supported&&mesh.source){
            if(mesh.windSource!=mesh.source){
                mesh.wind.reset();
                if(mesh.source->treeWind.empty())mesh.windSource=mesh.source;
                else if(admit(mesh.source->vertices.size()*(sizeof(MeshVertex)*2+sizeof(TreeWindVertex)))){
                    mesh.wind=std::make_shared<GpuTreeWind>(device_,rhi::defaultShaderDirectory(),gpu,mesh.source);mesh.windSource=mesh.source;
                }
            }
            if(mesh.wind&&animatedMeshes.insert(source->id).second){mesh.wind->record(windCommands,result.frame.timeSeconds,result.frame.treeWind);recordedWind=true;}
        }
        if (draw.subdivision) {
            auto key = std::make_pair(draw.objectId, source->id);
            usedSubdivisions.insert(key);
            auto &cached = cache_->subdivisions[key];
            if (cached.source != source || cached.material != draw.material) {
                auto uploaded = std::make_shared<GpuSubdivision>(
                    device_, rhi::defaultShaderDirectory(), source->vertices, source->indices,
                    draw.material && draw.material->height ? *draw.material->height : ImageRGBA8{});
                cached.source = source;
                cached.material = draw.material;
                cached.gpu = std::move(uploaded);
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
        if(result.frame.clusters.enabled&&!draw.subdivision&&!draw.wireframe&&draw.pathTracingKind==0&&source->treeWind.empty()&&
           mesh.source==source&&source->indices.size()/3>=128&&device_->computeLimits().supported){
            if(mesh.clusterSource!=source){
                mesh.clusters.reset();mesh.clusterBuild={};
                // Dedicated bounded CPU workers; never block the render owner on
                // construction or execute GPU calls on a worker.
                static engine::JobSystem clusterJobs(2,16);
                auto task=std::make_shared<std::packaged_task<std::shared_ptr<const ClusterHierarchy>()>>([source]{
                    ClusterBuildSettings settings;glm::vec3 low(INFINITY),high(-INFINITY);
                    for(const auto& v:source->vertices){low=glm::min(low,v.position);high=glm::max(high,v.position);}
                    // Import units differ (e.g. Sponza centimetres vs engine
                    // metres). Build a relative ceiling; runtime pixel error
                    // still determines which representations may be selected.
                    settings.maxError=std::max(1e-5f,glm::length(high-low)*.02f);
                    return buildClusterHierarchy(*source,settings);
                });
                auto future=task->get_future().share();
                if(clusterJobs.tryEnqueue([task]{(*task)();})){mesh.clusterSource=source;mesh.clusterBuild=std::move(future);mesh.clusters.reset();}
            }
            if(mesh.clusterBuild.valid()&&mesh.clusterBuild.wait_for(std::chrono::seconds(0))==std::future_status::ready){
                try{auto hierarchy=mesh.clusterBuild.get();if(!hierarchy->indices.empty()){mesh.clusters=std::make_shared<GpuClusterGeometry>(device_,hierarchy);++cache_->uploadEpoch;}}
                catch(const std::exception& error){std::cerr<<"Cluster geometry fallback: "<<error.what()<<'\n';}
                mesh.clusterBuild={};
            }
            if(mesh.clusters)result.packets.back().clusters=mesh.clusters;
        }
    }
    if(recordedWind)device_->submit(windCommands);
    for (auto it = cache_->meshes.begin(); it != cache_->meshes.end();)
        if (!usedMeshes.count(it->first))
            it = cache_->meshes.erase(it);
        else
            ++it;
    for (auto it = cache_->materials.begin(); it != cache_->materials.end();) {
        auto &variants = it->second;
        variants.erase(std::remove_if(variants.begin(), variants.end(), [](const auto &record) { return !record.used; }), variants.end());
        result.rasterMaterials.liveVariants += variants.size();
        if (variants.empty()) it = cache_->materials.erase(it);
        else ++it;
    }
    for (auto it = cache_->subdivisions.begin(); it != cache_->subdivisions.end();)
        if (!usedSubdivisions.count(it->first))
            it = cache_->subdivisions.erase(it);
        else
            ++it;
    const auto sourcePackets=result.packets.size();
    const bool culling=snapshot.rasterFrustumCulling&&!result.frame.taa;
    const bool batching=snapshot.rasterInstanceBatching&&device_->computeLimits().supported;
    if(culling){
        // Keep all casters/receivers for shadows, RSM and each water capture.
        if(result.frame.shadows||result.frame.rsm||!result.frame.oceans.empty()){
            result.auxiliaryPackets=batching?auxiliaryBatches_.batch(result.packets):result.packets;
        }else auxiliaryBatches_.batch({});
        result.packets.erase(std::remove_if(result.packets.begin(),result.packets.end(),[&](const auto& p){return !rasterPacketVisible(p,result.frame);}),result.packets.end());
        result.rasterCulledPackets=sourcePackets-result.packets.size();
    }else auxiliaryBatches_.batch({});
    if(batching){result.packets=batches_.batch(result.packets);result.rasterBatches=batches_.statistics();}
    else {batches_.batch({});result.rasterBatches.outputPackets=result.packets.size();}
    result.rasterBatches.sourcePackets=sourcePackets;
    if(culling&&batching&&!result.auxiliaryPackets.empty())result.rasterBatches.poseUploadBytes+=auxiliaryBatches_.statistics().poseUploadBytes;
    cache_->images->trim();
    result.gpuImages = cache_->images->stats();
    result.meshUploadsPending = uint32_t(pendingMeshIds.size());
    result.resolveCpuMilliseconds = elapsed();
    if(result.frame.clusters.enabled){uint32_t bits=0;std::memcpy(&bits,&result.frame.clusters.pixelError,4);result.frame.historyKey^=0xc1a57e0000000000ull^bits^(result.frame.clusters.wireframe?0x100000000ull:0)^(result.frame.clusters.occlusion?0x200000000ull:0)^(result.frame.clusters.batched?0x400000000ull:0)^(result.frame.clusters.twoPhase?0x800000000ull:0);}
    for(auto& ocean:result.frame.oceans)if(ocean.waveParticles.enabled)result.frame.historyKey^=ocean.id*0xdb4f0b9175ae2165ull;
    result.frame.historyKey ^= cache_->uploadEpoch * 0x94d049bb133111ebull;
    return result;
}
} // namespace render

namespace render {
void SceneAdapter::recordVirtualFeedback(const FrameData& frame,rhi::TextureViewHandle depth,const std::vector<glm::mat4>& auxiliaryViews) {
    device_->checkThread();if(!cache_->terrain)return;
    auto& record=*cache_->terrain;const auto height=record.gpu->heightTexture();
    std::vector<GpuVirtualTexture::VisibilityView> views;for(const auto& vp:auxiliaryViews)views.push_back({vp,128,128});
    height->setAuxiliaryViews(views);record.virtualMaterial->setAuxiliaryViews(std::move(views));
    // Model is supplied by the current terrain packet, not by a mutable Component.
    auto model=record.feedbackModel;
    height->recordFeedback(depth,glm::inverse(frame.viewProjection),model,frame.viewportWidth,frame.viewportHeight,height->minimum(),height->maximum());
    record.virtualMaterial->recordFeedback(depth,glm::inverse(frame.viewProjection),model,frame.viewportWidth,frame.viewportHeight,height->minimum(),height->maximum(),true);
}
std::vector<VegetationBatchStats> SceneAdapter::vegetationStats(){return cache_->terrain&&cache_->terrain->vegetation?cache_->terrain->vegetation->readStats():std::vector<VegetationBatchStats>{};}

}
