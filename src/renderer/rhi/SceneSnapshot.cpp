#include "renderer/rhi/SceneSnapshot.h"
#include "engine/JobSystem.h"
#include "rhi/ShaderAssets.h"
#include <algorithm>
#include "renderer/RenderScene.h"
#include "renderer/Material.h"
#include "renderer/Texture.h"
#include "component/GameObject.h"
#include "component/Mesh_Filter.h"
#include "component/Mesh_Renderer.h"
#include "component/transform.h"
#include "component/Lights.h"
#include "component/Ocean.h"
#include "component/Atmosphere.h"
#include "object/SkyBox.h"
#include "object/Terrain.h"
#include "component/TerrainComponent.h"
#include "component/Grass.h"
#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/GpuGrass.h"
#include "renderer/rhi/GpuSubdivision.h"
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include "utils/Camera.h"
#include <glm/gtx/euler_angles.hpp>
#include <filesystem>
#include <unordered_set>
#include <map>

namespace render {
namespace {
const char *names[] = {"material.albedo", "material.normal", "material.metallic", "material.roughness",
                       "material.ao"};
bool hasImage(const Material &m, const char *name) {
    return m.texture_path.count(name) || (m.textures.count(name) && m.textures.at(name));
}
MaterialParameters parameters(const Material &m) {
    MaterialParameters p;
    p.albedoAlpha = glm::vec4(m.albedoFactor, m.opacityFactor);
    p.factors = {m.metallicFactor.value_or(hasImage(m, names[2]) ? 1.f : 0.f),
                 m.roughnessFactor.value_or(hasImage(m, names[3]) ? 1.f : .5f), m.occlusionStrength,
                 m.alphaCutoff};
    p.emissiveNormal = glm::vec4(m.emissiveFactor, hasImage(m, names[1]) ? m.normalStrength : 0.f);
    return p;
}
std::shared_ptr<const ImageRGBA8> decodeShared(const Material &m, const char *name) {
    auto path = m.texture_path.find(name);
    if (path != m.texture_path.end())
        return ImageRGBA8::loadShared(path->second);
    auto texture = m.textures.find(name);
    if (texture == m.textures.end() || !texture->second)
        return {};
    const auto &t = *texture->second;
    if (!t.name.empty() && std::filesystem::is_regular_file(t.name))
        return ImageRGBA8::loadShared(t.name);
    if (!t.data || t.width <= 0 || t.height <= 0 || t.channels < 1 || t.channels > 4 ||
        (t.format != GL_RED && t.format != GL_RG && t.format != GL_RGB && t.format != GL_RGBA))
        throw std::invalid_argument("Renderer: texture needs decoded CPU pixels or a supported image path: " +
                                    t.name);
    ImageRGBA8 image{uint32_t(t.width), uint32_t(t.height),
                     std::vector<uint8_t>(size_t(t.width) * t.height * 4)};
    for (int y = 0; y < t.height; ++y)
        for (int x = 0; x < t.width; ++x) {
            const auto *source = t.data + (size_t(t.height - 1 - y) * t.width + x) * t.channels;
            auto *target = image.pixels.data() + (size_t(y) * t.width + x) * 4;
            for (int c = 0; c < 3; ++c)
                target[c] = source[t.channels < 3 ? 0 : c];
            target[3] = t.channels == 4 ? source[3] : t.channels == 2 ? source[1] : 255;
        }
    return std::make_shared<const ImageRGBA8>(std::move(image));
}
ImageRGBA8 decode(const Material &m, const char *name) {
    auto image = decodeShared(m, name);
    return image ? *image : ImageRGBA8{};
}
MaterialExtension extension(ShaderType type, const Material &material) {
    MaterialExtension e;
    e.settings.w = material.twoSided ? 1.f : 0.f;
    if (type == ShaderType::PBR_CLEARCOAT)
        e.lobes.x = 1;
    if (type == ShaderType::PBR_ANISOTROPY)
        e.lobes.z = .95f;
    if (type == ShaderType::PBR_SSS || material.hasSubSurface)
        e.lobes.w = 1;
    if (type == ShaderType::SIMPLE || type == ShaderType::LIGHT || type == ShaderType::TEST)
        e.settings.z = 1;
    return e;
}
ImageRGBA8 specialMaps(const Material &m) {
    const char *maps[] = {"material.clearCoatRoughness", "material.anisotropy", "material.height",
                          "material.thickness"};
    std::array<ImageRGBA8, 4> source;
    uint32_t width = 1, height = 1;
    for (size_t i = 0; i < 4; ++i) {
        source[i] = decode(m, maps[i]);
        width = std::max(width, source[i].width);
        height = std::max(height, source[i].height);
    }
    ImageRGBA8 result{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
    const uint8_t fallback[] = {255, 255, 0, 255};
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            for (size_t c = 0; c < 4; ++c) {
                const auto &image = source[c];
                result.pixels[(size_t(y) * width + x) * 4 + c] =
                    image.pixels.empty()
                        ? fallback[c]
                        : image.pixels[(size_t(uint64_t(y) * image.height / height) * image.width +
                                        uint64_t(x) * image.width / width) *
                                       4];
            }
    return result;
}
} // namespace
namespace {
template <class T> struct PendingPayload {
    uint64_t revision = 0;
    std::shared_future<std::shared_ptr<const T>> pending;
    std::shared_ptr<const T> value;
};
// Copy mutable in-memory texture bytes before dispatching to workers. File paths
// need no pixel copy and are read by the decoder job.
Material detachMaterial(const Material &material) {
    Material copy = material;
    for (auto &entry : copy.textures) {
        if (!entry.second || copy.texture_path.count(entry.first) ||
            (!entry.second->name.empty() && std::filesystem::is_regular_file(entry.second->name)))
            continue;
        const auto &original = *entry.second;
        auto texture = std::make_shared<Texture>();
        texture->name = original.name;
        texture->width = original.width;
        texture->height = original.height;
        texture->channels = original.channels;
        texture->format = original.format;
        texture->internalformat = original.internalformat;
        if (original.data && original.width > 0 && original.height > 0 && original.channels > 0 &&
            original.channels <= 4) {
            size_t bytes = size_t(original.width) * original.height * original.channels;
            texture->data = static_cast<unsigned char *>(std::malloc(bytes));
            if (!texture->data)
                throw std::bad_alloc();
            std::memcpy(texture->data, original.data, bytes);
        }
        entry.second = std::move(texture);
    }
    return copy;
}
template <class T, class F>
std::shared_ptr<const T> requestPayload(PendingPayload<T> &record, uint64_t revision, F &&build, bool wait) {
    if ((!record.pending.valid() && !record.value) || record.revision != revision) {
        record.revision = revision;
        record.value.reset();
        auto task = std::make_shared<std::packaged_task<std::shared_ptr<const T>()>>(std::forward<F>(build));
        auto future = task->get_future().share();
        if (wait) {
            engine::JobSystem::io().submit([task] { (*task)(); });
        } else if (!engine::JobSystem::io().tryEnqueue([task] { (*task)(); })) {
            record.pending = {};
            return {};
        }
        record.pending = std::move(future);
    }
    if (!record.value) {
        if (!wait && record.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return {};
        record.value = record.pending.get();
    }
    return record.value;
}
} // namespace
struct SceneSnapshotBuilder::State {
    uint64_t sequence = 0, epoch = 1;
    std::weak_ptr<const RenderWorldSnapshot> primed;
    std::unordered_map<uint64_t, PendingPayload<MeshPayload>> meshes;
    std::unordered_map<uint64_t, PendingPayload<MaterialPayload>> materials;
    PendingPayload<TerrainPayload> terrain;
    uint64_t terrainKey = 0;
    std::weak_ptr<Atmosphere> sunAtmosphere;
    float lastSunAngle = 0, lastSunAzimuth = 0;
};
SceneSnapshotBuilder::SceneSnapshotBuilder() : state_(std::make_unique<State>()) {}
SceneSnapshotBuilder::~SceneSnapshotBuilder() = default;
void SceneSnapshotBuilder::invalidateAssets() {
    state_->meshes.clear();
    state_->materials.clear();
    state_->terrain = {};
    ++state_->epoch;
}
std::shared_ptr<const RenderWorldSnapshot>
SceneSnapshotBuilder::capture(const std::shared_ptr<RenderScene> &scene, float time, uint32_t width,
                              uint32_t height, bool wait) {
    if (!width || !height)
        throw std::invalid_argument("Snapshot viewport is empty");
    if (scene)
        scene->checkLogicThread();
    if (scene && scene->preparedAssets && state_->primed.lock() != scene->preparedAssets) {
        const auto &prepared = *scene->preparedAssets;
        for (const auto &draw : prepared.draws) {
            if (draw.mesh) {
                auto &record = state_->meshes[draw.mesh->id];
                record.revision = draw.mesh->revision;
                record.value = draw.mesh;
                record.pending = {};
            }
            if (draw.material) {
                auto &record = state_->materials[draw.material->id];
                record.revision = draw.material->revision;
                record.value = draw.material;
                record.pending = {};
            }
        }
        if (prepared.terrain) {
            state_->terrainKey = prepared.terrain->source->id;
            state_->terrain.revision = prepared.terrain->source->revision;
            state_->terrain.value = prepared.terrain->source;
            state_->terrain.pending = {};
        }
        state_->primed = scene->preparedAssets;
        scene->preparedAssets
            .reset(); // Cache now owns payloads; do not retain removed assets through the bootstrap snapshot.
    }
    if (!scene || !scene->main_camera)
        throw std::invalid_argument("Renderer: scene needs a camera");
    const auto &camera = *scene->main_camera;
    RenderWorldSnapshot result;
    glm::mat4 depthConversion(1);
    depthConversion[2][2] = .5f;
    depthConversion[3][2] = .5f;
    result.frame.viewProjection = depthConversion * camera.GetPerspective() * camera.GetViewMatrix();
    result.frame.cameraPosition = camera.Position;
    result.frame.view = camera.GetViewMatrix();
    result.frame.nearPlane = camera.zNear;
    result.frame.farPlane = camera.zFar;
    result.exposure = camera.exposure;
    result.frame.viewportWidth = width;
    result.frame.viewportHeight = height;
    std::vector<std::shared_ptr<GameObject>> objects;
    std::vector<std::shared_ptr<DirectionLight>> directional;
    std::vector<std::shared_ptr<PointLight>> points;
    std::vector<std::shared_ptr<SpotLight>> spots;
    {
        std::scoped_lock guard(scene->mtx, scene->lightMtx);
        objects = scene->objects;
        directional = scene->directionLights;
        points = scene->pointLights;
        spots = scene->spotLights;
    }
    if (scene->terrain && std::find(objects.begin(), objects.end(), scene->terrain) == objects.end())
        objects.push_back(scene->terrain);
    // DirectionLight is authoritative initially; subsequent angle controls update that same light.
    auto atmo = scene->sky ? scene->sky->getComponent<Atmosphere>() : nullptr;
    auto source =
        std::find_if(directional.begin(), directional.end(), [](const auto &l) { return l && l->enabled; });
    if (atmo && source != directional.end()) {
        auto light = *source;
        if (state_->sunAtmosphere.lock() == atmo &&
            (atmo->sunAngle != state_->lastSunAngle || atmo->sunAzimuth != state_->lastSunAzimuth)) {
            float elevation = glm::radians(atmo->sunAngle), azimuth = glm::radians(atmo->sunAzimuth);
            light->data.direction = -glm::vec3(std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                                               -std::cos(elevation) * std::cos(azimuth));
        } else {
            if (glm::dot(light->data.direction, light->data.direction) < 1e-10f)
                throw std::invalid_argument("Sun needs a nonzero direction");
            auto sun = -glm::normalize(light->data.direction);
            atmo->sunAngle = glm::degrees(std::asin(glm::clamp(sun.y, -1.f, 1.f)));
            atmo->sunAzimuth = glm::dot(glm::vec2(sun.x, sun.z), glm::vec2(sun.x, sun.z)) < 1e-10f
                                   ? atmo->sunAzimuth
                                   : glm::degrees(std::atan2(sun.x, -sun.z));
        }
        state_->sunAtmosphere = atmo;
        state_->lastSunAngle = atmo->sunAngle;
        state_->lastSunAzimuth = atmo->sunAzimuth;
    }
    for (const auto &l : directional)
        if (l && l->enabled)
            result.frame.lights.push_back(
                {{0, 0, 0, 0}, glm::vec4(l->data.color, 0), glm::vec4(l->data.direction, 0)});
    for (const auto &l : points)
        if (l && l->enabled) {
            auto t = l->owner()->getComponent<Transform>();
            if (!t)
                throw std::invalid_argument("Renderer: light needs transform");
            result.frame.lights.push_back(
                {glm::vec4(t->position, 1), glm::vec4(l->data.color, 0), {0, 0, 0, 0}});
        }
    for (const auto &l : spots)
        if (l && l->enabled) {
            auto t = l->owner()->getComponent<Transform>();
            if (!t)
                throw std::invalid_argument("Renderer: light needs transform");
            result.frame.lights.push_back({glm::vec4(t->position, 2),
                                           glm::vec4(l->data.color, l->data.cutOff),
                                           glm::vec4(l->data.direction, l->data.outerCutOff)});
        }
    result.frame.shadows = result.frame.ssao = result.frame.rsm = true;
    result.frame.inverseSquareLocalLights = true;
    result.frame.timeSeconds = time;
    result.frame.taa = true;
    result.frame.historyKey = scene->assetId ^ (scene->revision() * 0x9e3779b97f4a7c15ull);
    if (scene->sky) {
        auto atmo = scene->sky->getComponent<Atmosphere>();
        if (atmo) {
            result.frame.sky = true;
            result.frame.sunAngle = atmo->sunAngle;
            result.frame.sunAzimuth = atmo->sunAzimuth;
            result.frame.seaLevelMeters = atmo->seaLevelMeters;
            result.frame.multipleScattering = atmo->multipleScattering;
            result.frame.groundAlbedo = atmo->groundAlbedo;
            const auto &a = atmo->atmosphere;
            auto &p = result.frame.atmosphere;
            p.radii = {a.solar_irradiance, a.sun_angular_radius, a.top_radius, a.bottom_radius};
            p.densities = {a.HDensityRayleigh, a.HDensityMie, a.OzoneCenter, a.mie_g};
            p.rayleigh = glm::vec4(a.rayleigh_scattering, 0);
            p.mie = glm::vec4(a.mie_scattering, 0);
            p.extinction = glm::vec4(a.mie_extinction, 0);
            p.absorption = glm::vec4(a.absorption_extinction, a.OzoneWidth);
        }
    }

    result.asynchronousStreaming = !wait;
    result.sequence = ++state_->sequence;
    result.frame.historyKey ^= state_->epoch * 0xd1b54a32d192ed03ull;
    bool ready = true;
    std::unordered_set<uint64_t> usedMeshes, usedMaterials;
    if (scene->terrain) {
        auto component = scene->terrain->getComponent<TerrainComponent>();
        if (component) {
            bool grass = bool(scene->terrain->getComponent<Grass>());
            uint64_t key = component->assetId;
            uint64_t revision = 0xcbf29ce484222325ull;
            auto mix = [&](uint64_t value) {
                revision ^= value + 0x9e3779b97f4a7c15ull + (revision << 6) + (revision >> 2);
            };
            mix(component->sourceRevision);
            mix(component->maxLeaves);
            mix(grass);
            // Paths are part of source identity; scalars and model are per-frame values.
            for (const auto &path : {component->heightSourcePath, component->heightVirtualTexture,
                                     component->materialVirtualTexture})
                mix(std::hash<std::string>{}(path));
            mix(component->heightWidth);
            mix(component->heightHeight);
            mix(reinterpret_cast<uintptr_t>(component->heightData));
            if (component->material) {
                mix(component->material->assetId);
                mix(component->material->contentRevision);
            }
            if (state_->terrainKey != key) {
                state_->terrain = {};
                state_->terrainKey = key;
            }
            // Capture only values. The worker does not access component or material.
            const auto heightPath = component->heightSourcePath, heightVT = component->heightVirtualTexture,
                       materialVT = component->materialVirtualTexture;
            auto w = component->heightWidth, h = component->heightHeight;
            auto capacity = component->maxLeaves;
            if (!w || !h) {
                auto material = component->terrainMaterial;
                if (!material || !material->textures.count("heightMap"))
                    throw std::invalid_argument("Terrain lacks height metadata");
                w = material->textures.at("heightMap")->width;
                h = material->textures.at("heightMap")->height;
            }
            std::vector<float> heights;
            std::shared_ptr<Material> material;
            const bool needsBuild = (!state_->terrain.pending.valid() && !state_->terrain.value) ||
                                    state_->terrain.revision != revision;
            if (needsBuild) {
                if (heightPath.empty() && heightVT.empty()) {
                    if (w < 2 || h < 2 || w > 16384 || h > 16384 || !component->heightData)
                        throw std::invalid_argument("Invalid CPU height field");
                    heights.assign(component->heightData, component->heightData + size_t(w) * h);
                }
                if (component->material)
                    material = std::make_shared<Material>(detachMaterial(*component->material));
            }
            auto source = requestPayload(
                state_->terrain, revision,
                [key, revision, grass, heightPath, heightVT, materialVT, w, h, capacity,
                 heights = std::move(heights), material] {
                    auto payload = std::make_shared<TerrainPayload>();
                    payload->id = key;
                    payload->revision = revision;
                    payload->capacity = capacity;
                    payload->grass = grass;
                    if (!heightVT.empty())
                        payload->height = packedVirtualSource(heightVT);
                    else if (!heightPath.empty())
                        payload->height = rawHeightVirtualSource(heightPath, w, h);
                    else
                        payload->height = heightVirtualSource(w, h, heights);
                    if (!materialVT.empty())
                        payload->material = packedVirtualSource(materialVT);
                    else {
                        std::array<ImageRGBA8, 5> images;
                        if (material)
                            for (size_t i = 0; i < 5; ++i)
                                images[i] = decode(*material, names[i]);
                        payload->material = materialVirtualSource(images);
                    }
                    payload->material.minimum = payload->height.minimum;
                    payload->material.maximum = payload->height.maximum;
                    for (auto *vt : {&payload->height, &payload->material}) {
                        uint32_t mip = 0;
                        for (uint32_t n = vt->extent / 64; n > 1; n /= 2)
                            ++mip;
                        vt->rootPage = vt->readPage(mip, 0, 0);
                    }
                    return std::shared_ptr<const TerrainPayload>(payload);
                },
                wait);
            SnapshotTerrain terrain;
            terrain.source = source;
            terrain.model = component->model;
            terrain.wireframe = component->polyMode == GL_LINE;
            terrain.parameters.factors = {0, .85f, 1, 0};
            terrain.parameters.albedoAlpha = {.3f, .45f, .2f, 1};
            if (component->material) {
                terrain.parameters = parameters(*component->material);
                if (!materialVT.empty()) {
                    terrain.parameters.emissiveNormal.w = component->material->normalStrength;
                    terrain.parameters.factors.x = component->material->metallicFactor.value_or(1.f);
                    terrain.parameters.factors.y = component->material->roughnessFactor.value_or(1.f);
                }
            }
            if (!source)
                ready = false;
            else
                result.terrain = std::move(terrain);
        }
    } else {
        state_->terrain = {};
        state_->terrainKey = 0;
    }
    for (const auto &object : objects)
        if (object) {
            auto ocean = object->getComponent<Ocean>();
            if (ocean) {
                OceanSurfaceSettings s;
                s.id = object->assetId;
                s.spectrum = {uint32_t(ocean->fft_size),
                              ocean->MeshLength,
                              ocean->A,
                              ocean->WindScale,
                              ocean->Lambda,
                              ocean->HeightScale,
                              ocean->BubblesScale,
                              ocean->BubblesThreshold,
                              glm::vec2(ocean->WindAndSeed),
                              ocean->seed};
                s.meshSize = uint32_t(ocean->MeshSize);
                s.seaLevel = ocean->seaLevel;
                s.timeScale = ocean->TimeScale;
                s.animate = ocean->animate;
                s.detailWaves = ocean->detailWaves;
                s.detailStrength = ocean->detailStrength;
                s.refraction = ocean->refraction;
                s.refractionStrength = ocean->refractionStrength;
                s.deepWaterDistance = ocean->deepWaterDistance;
                s.subsurfaceStrength = ocean->subsurfaceStrength;
                s.anisotropy = ocean->scatteringAnisotropy;
                s.absorption = ocean->absorption;
                s.scattering = ocean->scattering;
                s.fresnel = ocean->outer_FresnelScale;
                s.gloss = float(ocean->outer_Gloss);
                s.shallow = ocean->outer_OceanColorShallow;
                s.deep = ocean->outer_OceanColorDeep;
                s.foamColor = ocean->outer_BubblesColor;
                s.specular = ocean->outer_Specular;
                s.ambient = ocean->outer_ambient;
                result.frame.oceans.push_back(s);
            }

            auto filter = object->getComponent<MeshFilter>();
            auto transform = object->getComponent<Transform>();
            if (!filter || !transform)
                continue;
            auto renderer = object->getComponent<MeshRenderer>();
            if (renderer && ((renderer->drawMode != GL_TRIANGLES && renderer->drawMode != GL_PATCHES) ||
                             (renderer->polyMode != GL_FILL && renderer->polyMode != GL_LINE)))
                throw std::invalid_argument("Native renderer needs triangles or subdivision patches");
            auto model =
                glm::translate(glm::mat4(1), transform->position) *
                glm::scale(glm::mat4(1), transform->scale) *
                glm::eulerAngleYXZ(glm::radians(transform->rotation.y), glm::radians(transform->rotation.x),
                                   glm::radians(transform->rotation.z));
            for (const auto &mesh : filter->meshes)
                if (mesh) {
                    auto &record = state_->meshes[mesh->assetId];
                    usedMeshes.insert(mesh->assetId);
                    std::vector<Vertex> vertices;
                    std::vector<unsigned> indices;
                    if ((!record.pending.valid() && !record.value) ||
                        record.revision != mesh->contentRevision) {
                        vertices = mesh->vertices;
                        indices = mesh->indices;
                    }
                    auto payload = requestPayload(
                        record, mesh->contentRevision,
                        [id = mesh->assetId, rev = mesh->contentRevision, vertices = std::move(vertices),
                         indices = std::move(indices)] {
                            auto result = std::make_shared<MeshPayload>();
                            result->id = id;
                            result->revision = rev;
                            std::vector<uint32_t> remap(vertices.size(), UINT32_MAX);
                            for (auto index : indices) {
                                if (index >= vertices.size())
                                    throw std::invalid_argument("Invalid CPU mesh index");
                                if (remap[index] == UINT32_MAX) {
                                    const auto &vertex = vertices[index];
                                    remap[index] = uint32_t(result->vertices.size());
                                    result->vertices.push_back(
                                        {vertex.Position,
                                         vertex.Normal,
                                         {vertex.TexCoords.x, 1 - vertex.TexCoords.y}});
                                }
                                result->indices.push_back(remap[index]);
                            }
                            return std::shared_ptr<const MeshPayload>(result);
                        },
                        wait);
                    SnapshotDraw draw;
                    draw.shading = uint32_t(renderer ? renderer->shaderType : ShaderType::PBR);
                    draw.objectId = object->assetId;
                    draw.mesh = payload;
                    draw.model = model;
                    draw.wireframe = renderer && renderer->polyMode == GL_LINE;
                    draw.subdivision = renderer && renderer->shaderType == ShaderType::PBR_TESS;
                    if (mesh->material) {
                        auto material = mesh->material;
                        auto &materialRecord = state_->materials[material->assetId];
                        usedMaterials.insert(material->assetId);
                        std::shared_ptr<Material> detached;
                        if ((!materialRecord.pending.valid() && !materialRecord.value) ||
                            materialRecord.revision != material->contentRevision)
                            detached = std::make_shared<Material>(detachMaterial(*material));
                        draw.material = requestPayload(
                            materialRecord, material->contentRevision,
                            [detached, id = material->assetId, rev = material->contentRevision] {
                                auto payload = std::make_shared<MaterialPayload>();
                                payload->id = id;
                                payload->revision = rev;
                                for (size_t i = 0; i < 5; ++i)
                                    payload->images[i] = decodeShared(*detached, names[i]);
                                payload->special = specialMaps(*detached);
                                payload->height = decodeShared(*detached, "material.height");
                                return std::shared_ptr<const MaterialPayload>(payload);
                            },
                            wait);
                        draw.parameters = parameters(*material);
                        draw.extension =
                            extension(renderer ? renderer->shaderType : ShaderType::PBR, *material);
                        if (!draw.material)
                            ready = false;
                    } else {
                        draw.parameters.factors = {0, .5f, 1, 0};
                        draw.parameters.emissiveNormal.w = 0;
                    }
                    if (!payload)
                        ready = false;
                    result.draws.push_back(std::move(draw));
                }
        }
    for (auto it = state_->meshes.begin(); it != state_->meshes.end();)
        if (!usedMeshes.count(it->first))
            it = state_->meshes.erase(it);
        else
            ++it;
    for (auto it = state_->materials.begin(); it != state_->materials.end();)
        if (!usedMaterials.count(it->first))
            it = state_->materials.erase(it);
        else
            ++it;
    ImageRGBA8::releaseUnused();
    if (!ready)
        return {};
    return std::make_shared<const RenderWorldSnapshot>(std::move(result));
}
} // namespace render
