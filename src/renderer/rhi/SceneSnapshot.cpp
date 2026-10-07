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
#include "component/Cloud.h"
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
    return m.getTexturePaths().count(name) || (m.getTextures().count(name) && m.getTextures().at(name));
}
MaterialParameters parameters(const Material &m) {
    MaterialParameters p;
    const auto props = m.properties();
    p.albedoAlpha = glm::vec4(props.albedoFactor, props.opacityFactor);
    p.factors = {props.metallicFactor.value_or(hasImage(m, names[2]) ? 1.f : 0.f),
                 props.roughnessFactor.value_or(hasImage(m, names[3]) ? 1.f : .5f), props.occlusionStrength,
                 props.alphaCutoff};
    p.emissiveNormal = glm::vec4(props.emissiveFactor, hasImage(m, names[1]) ? props.normalStrength : 0.f);
    return p;
}
std::shared_ptr<const ImageRGBA8> decodeShared(const MaterialData &m, const char *name) {
    auto path = m.texture_path.find(name);
    if (path != m.texture_path.end())
        return ImageRGBA8::loadShared(path->second);
    auto texture = m.textures.find(name);
    if (texture == m.textures.end())
        return {};
    const auto &t = texture->second;
    if (!t.name.empty() && std::filesystem::is_regular_file(t.name))
        return ImageRGBA8::loadShared(t.name);
    if (!t.data() || t.width <= 0 || t.height <= 0 || t.channels < 1 || t.channels > 4 ||
        (t.format != GL_RED && t.format != GL_RG && t.format != GL_RGB && t.format != GL_RGBA))
        throw std::invalid_argument("Renderer: texture needs decoded CPU pixels or a supported image path: " +
                                    t.name);
    ImageRGBA8 image{uint32_t(t.width), uint32_t(t.height),
                     std::vector<uint8_t>(size_t(t.width) * t.height * 4)};
    for (int y = 0; y < t.height; ++y)
        for (int x = 0; x < t.width; ++x) {
            const auto *source = t.data() + (size_t(t.height - 1 - y) * t.width + x) * t.channels;
            auto *target = image.pixels.data() + (size_t(y) * t.width + x) * 4;
            for (int c = 0; c < 3; ++c)
                target[c] = source[t.channels < 3 ? 0 : c];
            target[3] = t.channels == 4 ? source[3] : t.channels == 2 ? source[1] : 255;
        }
    return std::make_shared<const ImageRGBA8>(std::move(image));
}
ImageRGBA8 decode(const MaterialData &m, const char *name) {
    auto image = decodeShared(m, name);
    return image ? *image : ImageRGBA8{};
}
MaterialExtension extension(ShaderType type, const MaterialProperties &material) {
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
ImageRGBA8 specialMaps(const MaterialData &m) {
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
MaterialData detachMaterial(const Material &material) {return material.snapshot();}
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
    std::unordered_map<uint64_t, PendingPayload<ImageRGBA8>> waterMasks;
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
    state_->waterMasks.clear();
    ++state_->epoch;
}
std::shared_ptr<const RenderWorldSnapshot>
SceneSnapshotBuilder::capture(const std::shared_ptr<RenderScene> &scene, float time, uint32_t width,
                              uint32_t height, bool wait) {
    if (!width || !height)
        throw std::invalid_argument("Snapshot viewport is empty");
    if (scene)
        scene->checkLogicThread();
    if (scene && scene->preparedAssets() && state_->primed.lock() != scene->preparedAssets()) {
        const auto &prepared = *scene->preparedAssets();
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
        state_->primed = scene->preparedAssets();
        scene->setPreparedAssets(
            {}); // Cache now owns payloads; do not retain removed assets through the bootstrap snapshot.
    }
    if (!scene || !scene->mainCamera())
        throw std::invalid_argument("Renderer: scene needs a camera");
    const auto &camera = *scene->mainCamera();
    RenderWorldSnapshot result;
    glm::mat4 depthConversion(1);
    depthConversion[2][2] = .5f;
    depthConversion[3][2] = .5f;
    result.frame.viewProjection = depthConversion * camera.GetPerspective() * camera.GetViewMatrix();
    result.frame.cameraPosition = camera.getPosition();
    result.frame.view = camera.GetViewMatrix();
    result.frame.nearPlane = camera.getNear();
    result.frame.farPlane = camera.getFar();
    result.exposure = camera.getExposure();
    result.frame.viewportWidth = width;
    result.frame.viewportHeight = height;
    std::vector<std::shared_ptr<GameObject>> objects;
    std::vector<std::shared_ptr<DirectionLight>> directional;
    std::vector<std::shared_ptr<PointLight>> points;
    std::vector<std::shared_ptr<SpotLight>> spots;
    {
        objects = scene->objects();
        directional = scene->directionLights();
        points = scene->pointLights();
        spots = scene->spotLights();
    }
    if (scene->terrain() && std::find(objects.begin(), objects.end(), scene->terrain()) == objects.end())
        objects.push_back(scene->terrain());
    // DirectionLight is authoritative initially; subsequent angle controls update that same light.
    auto atmo = scene->sky() ? scene->sky()->getComponent<Atmosphere>() : nullptr;
    auto source = std::find_if(directional.begin(), directional.end(),
                               [](const auto &l) { return l && l->isEnabled(); });
    if (atmo && source != directional.end()) {
        auto light = *source;
        if (state_->sunAtmosphere.lock() == atmo &&
            (atmo->settings().sunAngle != state_->lastSunAngle || atmo->settings().sunAzimuth != state_->lastSunAzimuth)) {
            float elevation = glm::radians(atmo->settings().sunAngle), azimuth = glm::radians(atmo->settings().sunAzimuth);
            light->setDirection(-glm::vec3(std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                                           -std::cos(elevation) * std::cos(azimuth)));
        } else {
            if (glm::dot(light->getData().direction, light->getData().direction) < 1e-10f)
                throw std::invalid_argument("Sun needs a nonzero direction");
            auto sun = -glm::normalize(light->getData().direction);
            atmo->updateSettings([&](auto& value){value.sunAngle= glm::degrees(std::asin(glm::clamp(sun.y, -1.f, 1.f)));});
            atmo->updateSettings([&](auto& value){value.sunAzimuth= glm::dot(glm::vec2(sun.x, sun.z), glm::vec2(sun.x, sun.z)) < 1e-10f
                                   ? atmo->settings().sunAzimuth
                                   : glm::degrees(std::atan2(sun.x, -sun.z));});
        }
        state_->sunAtmosphere = atmo;
        state_->lastSunAngle = atmo->settings().sunAngle;
        state_->lastSunAzimuth = atmo->settings().sunAzimuth;
    }
    for (const auto &l : directional)
        if (l && l->isEnabled())
            result.frame.lights.push_back(
                {{0, 0, 0, 0}, glm::vec4(l->getData().color, 0), glm::vec4(l->getData().direction, 0)});
    for (const auto &l : points)
        if (l && l->isEnabled()) {
            auto t = l->owner()->getComponent<Transform>();
            if (!t)
                throw std::invalid_argument("Renderer: light needs transform");
            result.frame.lights.push_back(
                {glm::vec4(t->getPosition(), 1), glm::vec4(l->getData().color, 0), {0, 0, 0, 0}});
        }
    for (const auto &l : spots)
        if (l && l->isEnabled()) {
            auto t = l->owner()->getComponent<Transform>();
            if (!t)
                throw std::invalid_argument("Renderer: light needs transform");
            result.frame.lights.push_back({glm::vec4(t->getPosition(), 2),
                                           glm::vec4(l->getData().color, l->getData().cutOff),
                                           glm::vec4(l->getData().direction, l->getData().outerCutOff)});
        }
    result.frame.shadows = result.frame.ssao = result.frame.rsm = true;
    result.frame.inverseSquareLocalLights = true;
    result.frame.timeSeconds = time;
    result.frame.taa = true;
    result.frame.historyKey = scene->assetId ^ (scene->revision() * 0x9e3779b97f4a7c15ull);
    if (scene->sky()) {
        if(auto cloud=scene->sky()->getComponent<Cloud>())result.frame.clouds=cloud->settings();
        auto atmo = scene->sky()->getComponent<Atmosphere>();
        if (atmo) {
            result.frame.sky = true;
            result.frame.sunAngle = atmo->settings().sunAngle;
            result.frame.sunAzimuth = atmo->settings().sunAzimuth;
            result.frame.seaLevelMeters = atmo->settings().seaLevelMeters;
            result.frame.multipleScattering = atmo->settings().multipleScattering;
            result.frame.groundAlbedo = atmo->settings().groundAlbedo;
            const auto &a = atmo->settings().atmosphere;
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
    std::unordered_set<uint64_t> usedMeshes, usedMaterials, usedMasks;
    if (scene->terrain()) {
        auto component = scene->terrain()->getComponent<TerrainComponent>();
        if (component) {
            auto grassComponent = scene->terrain()->getComponent<Grass>();
            bool grass = bool(grassComponent);
            auto vegetation = grass ? grassComponent->settings() : VegetationSettings{};
            const auto shoreline = component->settings().shoreline;
            const bool water=bool(scene->terrain()->getComponent<Ocean>());
            if(shoreline.enabled) {
                vegetation.exclusionSeaLevel=shoreline.seaLevel;vegetation.exclusionHeightRange=shoreline.heightRange;
                vegetation.exclusionSlopeMin=shoreline.slopeMin;vegetation.exclusionSlopeMax=shoreline.slopeMax;
            }
            uint64_t key = component->assetId;
            uint64_t revision = 0xcbf29ce484222325ull;
            auto mix = [&](uint64_t value) {
                revision ^= value + 0x9e3779b97f4a7c15ull + (revision << 6) + (revision >> 2);
            };
            mix(component->getSourceRevision());
            mix(component->settings().maxLeaves);
            mix(grass);
            mix(water);
            mix(shoreline.enabled);
            if(shoreline.enabled)for(auto& path:shoreline.paths)mix(std::hash<std::string>{}(path));
            if (grass) {
                mix(vegetation.capacity);
                mix(std::hash<std::string>{}(vegetation.waterMaskPath));
            }
            // Paths are part of source identity; scalars and model are per-frame values.
            for (const auto &path : {component->settings().heightSourcePath, component->settings().heightVirtualTexture,
                                     component->settings().materialVirtualTexture})
                mix(std::hash<std::string>{}(path));
            mix(component->getHeightWidth());
            mix(component->getHeightHeight());
            mix(reinterpret_cast<uintptr_t>(component->getHeightData()));
            if (component->settings().material) {
                mix(component->settings().material->assetId);
                mix(component->settings().material->getContentRevision());
            }
            if (state_->terrainKey != key) {
                state_->terrain = {};
                state_->terrainKey = key;
            }
            // Capture only values. The worker does not access component or material.
            const auto heightPath = component->settings().heightSourcePath, heightVT = component->settings().heightVirtualTexture,
                       materialVT = component->settings().materialVirtualTexture;
            auto w = component->getHeightWidth(), h = component->getHeightHeight();
            auto capacity = component->settings().maxLeaves;
            if (!w || !h) {
                auto material = component->settings().terrainMaterial;
                if (!material || !material->getTextures().count("heightMap"))
                    throw std::invalid_argument("Terrain lacks height metadata");
                w = material->getTextures().at("heightMap")->getWidth();
                h = material->getTextures().at("heightMap")->getHeight();
            }
            std::vector<float> heights;
            std::shared_ptr<MaterialData> material;
            const bool needsBuild = (!state_->terrain.pending.valid() && !state_->terrain.value) ||
                                    state_->terrain.revision != revision;
            if (needsBuild) {
                if (heightPath.empty() && heightVT.empty()) {
                    if (w < 2 || h < 2 || w > 16384 || h > 16384 || !component->getHeightData())
                        throw std::invalid_argument("Invalid CPU height field");
                    heights.assign(component->getHeightData(), component->getHeightData() + size_t(w) * h);
                }
                if (component->settings().material)
                    material = std::make_shared<MaterialData>(detachMaterial(*component->settings().material));
            }
            auto source = requestPayload(
                state_->terrain, revision,
                [key, revision, grass, water, vegetation, shoreline, heightPath, heightVT, materialVT, w, h, capacity,
                 heights = std::move(heights), material] {
                    auto payload = std::make_shared<TerrainPayload>();
                    payload->id = key;
                    payload->revision = revision;
                    payload->capacity = capacity;
                    payload->grass = grass;
                    payload->vegetation = vegetation;
                    if(shoreline.enabled)for(size_t i=0;i<4;++i)payload->shorelineImages[i]=ImageRGBA8::loadShared(shoreline.paths[i]);
                    if (!vegetation.waterMaskPath.empty())
                        payload->waterMask = ImageRGBA8::loadShared(vegetation.waterMaskPath);
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
                    if(water)payload->bathymetry=prepareWaterBathymetry(payload->height,payload->material,material?material->albedoFactor:glm::vec3(.3f,.45f,.2f));
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
            terrain.vegetation = vegetation;
            terrain.extension.shoreHeight={shoreline.seaLevel,shoreline.heightRange,shoreline.wetBelow,shoreline.wetAbove};
            terrain.extension.shoreSurface={shoreline.textureLength,shoreline.slopeMin,shoreline.slopeMax,shoreline.normalStrength};
            terrain.source = source;
            terrain.model = component->settings().model;
            terrain.wireframe = component->settings().polyMode == GL_LINE;
            terrain.parameters.factors = {0, .85f, 1, 0};
            terrain.parameters.albedoAlpha = {.3f, .45f, .2f, 1};
            if (component->settings().material) {
                terrain.parameters = parameters(*component->settings().material);
                if (!materialVT.empty()) {
                    terrain.parameters.emissiveNormal.w = component->settings().material->getNormalStrength();
                    terrain.parameters.factors.x = component->settings().material->getMetallicFactor().value_or(1.f);
                    terrain.parameters.factors.y = component->settings().material->getRoughnessFactor().value_or(1.f);
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
                const auto configuration = ocean->settings();
                OceanSurfaceSettings s;
                s.id = object->assetId;
                s.spectrum = {uint32_t(configuration.fft_size),
                              configuration.SpectrumLength>0 ? configuration.SpectrumLength : configuration.MeshLength,
                              configuration.A,
                              configuration.WindScale,
                              configuration.Lambda,
                              configuration.HeightScale,
                              configuration.BubblesScale,
                              configuration.BubblesThreshold,
                              glm::vec2(configuration.WindAndSeed),
                              configuration.seed};
                s.meshSize = uint32_t(configuration.MeshSize);
                s.surfaceLength = configuration.MeshLength;
                s.seaLevel = configuration.seaLevel;
                s.timeScale = configuration.TimeScale;
                s.animate = configuration.animate;
                s.detailWaves = configuration.detailWaves;
                s.detailStrength = configuration.detailStrength;
                s.shortWaveRipples=configuration.shortWaveRipples;
                s.bedCaustics=configuration.bedCaustics;s.causticStrength=configuration.causticStrength;
                s.causticCascades=configuration.causticCascades;s.causticMeshReceivers=configuration.causticMeshReceivers;
                s.underwaterWideRefraction=configuration.underwaterWideRefraction;s.underwaterSunShafts=configuration.underwaterSunShafts;s.sunShaftStrength=configuration.sunShaftStrength;
                s.underwaterParticles=configuration.underwaterParticles;s.particleDensity=configuration.particleDensity;s.underwaterVolumeSteps=configuration.underwaterVolumeSteps;
                s.rippleRmsHeight=configuration.rippleRmsHeight;
                s.cameraGrid = configuration.cameraGrid;
                s.gridFocus = configuration.gridFocus;
                s.underwaterCapture = configuration.underwaterCapture;
                s.volumeIntegration = configuration.volumeIntegration;
                s.robustRefraction=configuration.robustRefraction;
                s.multipleScattering=configuration.multipleScattering;
                s.underwaterView=configuration.underwaterView;s.underwaterFog=configuration.underwaterFog;
                s.opticalDebug=configuration.opticalDebug;
                s.shore=configuration.shore;
                if(result.terrain){s.bathymetry=result.terrain->source->bathymetry;s.bathymetryModel=result.terrain->model;}
                s.refraction = configuration.refraction;
                s.refractionStrength = configuration.refractionStrength;
                s.deepWaterDistance = configuration.deepWaterDistance;
                s.subsurfaceStrength = configuration.subsurfaceStrength;
                s.anisotropy = configuration.scatteringAnisotropy;
                s.absorption = configuration.absorption;
                s.scattering = configuration.scattering;
                s.fresnel = configuration.outer_FresnelScale;
                s.gloss = float(configuration.outer_Gloss);
                s.shallow = configuration.outer_OceanColorShallow;
                s.deep = configuration.outer_OceanColorDeep;
                s.foamColor = configuration.outer_BubblesColor;
                s.specular = configuration.outer_Specular;
                s.ambient = configuration.outer_ambient;
                const auto maskPath = configuration.waterMaskPath;
                if (!maskPath.empty()) {
                    usedMasks.insert(object->assetId);
                    s.waterMask = requestPayload(state_->waterMasks[object->assetId],
                        std::hash<std::string>{}(maskPath),
                        [maskPath] { return ImageRGBA8::loadShared(maskPath); }, wait);
                    ready = ready && bool(s.waterMask);
                }
                result.frame.oceans.push_back(s);
            }

            auto filter = object->getComponent<MeshFilter>();
            auto transform = object->getComponent<Transform>();
            if (!filter || !transform)
                continue;
            auto renderer = object->getComponent<MeshRenderer>();
            if (renderer &&
                ((renderer->getDrawMode() != GL_TRIANGLES && renderer->getDrawMode() != GL_PATCHES) ||
                 (renderer->getPolyMode() != GL_FILL && renderer->getPolyMode() != GL_LINE)))
                throw std::invalid_argument("Native renderer needs triangles or subdivision patches");
            auto model = glm::translate(glm::mat4(1), transform->getPosition()) *
                         glm::scale(glm::mat4(1), transform->getScale()) *
                         glm::eulerAngleYXZ(glm::radians(transform->getRotation().y),
                                            glm::radians(transform->getRotation().x),
                                            glm::radians(transform->getRotation().z));
            for (const auto &mesh : filter->getMeshes())
                if (mesh) {
                    auto &record = state_->meshes[mesh->assetId];
                    usedMeshes.insert(mesh->assetId);
                    std::vector<Vertex> vertices;
                    std::vector<unsigned> indices;
                    if ((!record.pending.valid() && !record.value) ||
                        record.revision != mesh->getContentRevision()) {
                        vertices = mesh->getVertices();
                        indices = mesh->getIndices();
                    }
                    auto payload = requestPayload(
                        record, mesh->getContentRevision(),
                        [id = mesh->assetId, rev = mesh->getContentRevision(), vertices = std::move(vertices),
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
                    draw.shading = uint32_t(renderer ? renderer->getShaderType() : ShaderType::PBR);
                    draw.objectId = object->assetId;
                    draw.mesh = payload;
                    draw.model = model;
                    draw.wireframe = renderer && renderer->getPolyMode() == GL_LINE;
                    draw.subdivision = renderer && renderer->getShaderType() == ShaderType::PBR_TESS;
                    if (mesh->getMaterial()) {
                        auto material = mesh->getMaterial();
                        auto &materialRecord = state_->materials[material->assetId];
                        usedMaterials.insert(material->assetId);
                        std::shared_ptr<MaterialData> detached;
                        if ((!materialRecord.pending.valid() && !materialRecord.value) ||
                            materialRecord.revision != material->getContentRevision())
                            detached = std::make_shared<MaterialData>(detachMaterial(*material));
                        draw.material = requestPayload(
                            materialRecord, material->getContentRevision(),
                            [detached, id = material->assetId, rev = material->getContentRevision()] {
                                auto payload = std::make_shared<MaterialPayload>();
                                payload->id = id;
                                payload->revision = rev;
                                for (size_t i = 0; i < 5; ++i)
                                    payload->images[i] = decodeShared(*detached, names[i]);
                                payload->special = std::make_shared<const ImageRGBA8>(specialMaps(*detached));
                                payload->height = decodeShared(*detached, "material.height");
                                return std::shared_ptr<const MaterialPayload>(payload);
                            },
                            wait);
                        draw.parameters = parameters(*material);
                        draw.extension = extension(renderer ? renderer->getShaderType() : ShaderType::PBR,
                                                   material->properties());
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
    for (auto it = state_->waterMasks.begin(); it != state_->waterMasks.end();)
        if (!usedMasks.count(it->first)) it = state_->waterMasks.erase(it);
        else ++it;
    ImageRGBA8::releaseUnused();
    if (!ready)
        return {};
    return std::make_shared<const RenderWorldSnapshot>(std::move(result));
}
} // namespace render
