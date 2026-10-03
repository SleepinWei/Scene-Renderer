#include "renderer/rhi/SceneAdapter.h"
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
#include "utils/Camera.h"
#include <glm/gtx/euler_angles.hpp>
#include <filesystem>
#include <unordered_set>
#include <map>
namespace render {
namespace {
const char* names[] = {"material.albedo","material.normal","material.metallic","material.roughness","material.ao"};
bool hasImage(const Material& m, const char* name) { return m.texture_path.count(name) || (m.textures.count(name) && m.textures.at(name)); }
MaterialParameters parameters(const Material& m) {
    MaterialParameters p;p.albedoAlpha = glm::vec4(m.albedoFactor,m.opacityFactor);
    p.factors = {m.metallicFactor.value_or(hasImage(m,names[2]) ? 1.f : 0.f),m.roughnessFactor.value_or(hasImage(m,names[3]) ? 1.f : .5f),m.occlusionStrength,m.alphaCutoff};
    p.emissiveNormal = glm::vec4(m.emissiveFactor,hasImage(m,names[1]) ? m.normalStrength : 0.f);return p;
}
ImageRGBA8 decode(const Material& m, const char* name) {
    auto path = m.texture_path.find(name);if (path != m.texture_path.end()) return ImageRGBA8::load(path->second);
    auto texture = m.textures.find(name);if (texture == m.textures.end() || !texture->second) return {};
    const auto& t = *texture->second;
    if (!t.name.empty() && std::filesystem::is_regular_file(t.name)) return ImageRGBA8::load(t.name);
    if (!t.data || t.width <= 0 || t.height <= 0 || t.channels < 1 || t.channels > 4 ||
        (t.format != GL_RED && t.format != GL_RG && t.format != GL_RGB && t.format != GL_RGBA))
        throw std::invalid_argument("Renderer: texture needs decoded CPU pixels or a supported image path: " + t.name);
    ImageRGBA8 image{uint32_t(t.width),uint32_t(t.height),std::vector<uint8_t>(size_t(t.width)*t.height*4)};
    for (int y = 0; y < t.height; ++y) for (int x = 0; x < t.width; ++x) {
        const auto* source = t.data + (size_t(t.height-1-y)*t.width+x)*t.channels;auto* target = image.pixels.data() + (size_t(y)*t.width+x)*4;
        for (int c = 0; c < 3; ++c) target[c] = source[t.channels < 3 ? 0 : c];target[3] = t.channels == 4 ? source[3] : t.channels == 2 ? source[1] : 255;
    }return image;
}
MaterialExtension extension(ShaderType type,const Material& material){
    MaterialExtension e;e.settings.w=material.twoSided?1.f:0.f;
    if(type==ShaderType::PBR_CLEARCOAT)e.lobes.x=1;
    if(type==ShaderType::PBR_ANISOTROPY)e.lobes.z=.95f;
    if(type==ShaderType::PBR_SSS || material.hasSubSurface)e.lobes.w=1;
    if(type==ShaderType::SIMPLE || type==ShaderType::LIGHT || type==ShaderType::TEST)e.settings.z=1;
    return e;
}
ImageRGBA8 specialMaps(const Material& m){
    const char* maps[]={"material.clearCoatRoughness","material.anisotropy","material.height","material.thickness"};
    std::array<ImageRGBA8,4> source;uint32_t width=1,height=1;
    for(size_t i=0;i<4;++i){source[i]=decode(m,maps[i]);width=std::max(width,source[i].width);height=std::max(height,source[i].height);}
    ImageRGBA8 result{width,height,std::vector<uint8_t>(size_t(width)*height*4)};const uint8_t fallback[]={255,255,0,255};
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)for(size_t c=0;c<4;++c){const auto& image=source[c];result.pixels[(size_t(y)*width+x)*4+c]=image.pixels.empty()?fallback[c]:image.pixels[(size_t(uint64_t(y)*image.height/height)*image.width+uint64_t(x)*image.width/width)*4];}return result;
}
}
struct SceneAdapter::Cache {
    struct MeshRecord { std::weak_ptr<Mesh> source;std::shared_ptr<GpuMesh> gpu; };
    struct MaterialRecord { std::weak_ptr<Material> source;std::shared_ptr<GpuMaterial> gpu; };
    std::unordered_map<const Mesh*,MeshRecord> meshes;
    using MaterialKey=std::pair<const Material*,ShaderType>;
    std::map<MaterialKey,MaterialRecord> materials;
    struct TerrainRecord{std::weak_ptr<TerrainComponent> source;std::shared_ptr<GpuTerrain> gpu;std::shared_ptr<GpuGrass> grass;std::shared_ptr<GpuMaterial> material,grassMaterial;};
    std::unique_ptr<TerrainRecord> terrain;
    struct SubdivisionRecord{std::weak_ptr<Mesh> source;std::shared_ptr<GpuSubdivision> gpu;};
    std::map<std::pair<const GameObject*,const Mesh*>,SubdivisionRecord> subdivisions;
    std::shared_ptr<GpuMaterial> fallback;
    std::weak_ptr<Atmosphere> sunAtmosphere;float lastSunAngle=0,lastSunAzimuth=0;
};
SceneAdapter::SceneAdapter(std::shared_ptr<rhi::GraphicsDevice> device) : device_(std::move(device)), cache_(std::make_unique<Cache>()) {
    MaterialDesc fallback;fallback.parameters.factors = {0,.5f,1,0};fallback.parameters.emissiveNormal.w = 0;
    cache_->fallback = std::make_shared<GpuMaterial>(device_,fallback);
}
SceneAdapter::~SceneAdapter() = default;
void SceneAdapter::invalidateAssets() { cache_->meshes.clear();cache_->materials.clear();cache_->terrain.reset();cache_->subdivisions.clear(); }
SceneFrame SceneAdapter::collect(const std::shared_ptr<RenderScene>& scene,float timeOverride) {
    if (!scene || !scene->main_camera) throw std::invalid_argument("Renderer: scene needs a camera");
    const auto& camera = *scene->main_camera;SceneFrame result;
    glm::mat4 depthConversion(1);depthConversion[2][2] = .5f;depthConversion[3][2] = .5f;
    result.frame.viewProjection = depthConversion * camera.GetPerspective() * camera.GetViewMatrix();result.frame.cameraPosition = camera.Position;result.frame.view=camera.GetViewMatrix();result.frame.nearPlane=camera.zNear;result.frame.farPlane=camera.zFar;result.exposure = camera.exposure;
    std::vector<std::shared_ptr<GameObject>> objects;
    std::vector<std::shared_ptr<DirectionLight>> directional;std::vector<std::shared_ptr<PointLight>> points;std::vector<std::shared_ptr<SpotLight>> spots;
    { std::scoped_lock guard(scene->mtx,scene->lightMtx);objects = scene->objects;directional = scene->directionLights;points = scene->pointLights;spots = scene->spotLights; }
    if(scene->terrain && std::find(objects.begin(),objects.end(),scene->terrain)==objects.end())objects.push_back(scene->terrain);
    // DirectionLight is authoritative initially; subsequent angle controls update that same light.
    auto atmo=scene->sky?std::static_pointer_cast<Atmosphere>(scene->sky->GetComponent("Atmosphere")):nullptr;
    auto source=std::find_if(directional.begin(),directional.end(),[](const auto& l){return l && l->enabled;});
    if(atmo && source!=directional.end()) {
        auto light=*source;
        if(cache_->sunAtmosphere.lock()==atmo && (atmo->sunAngle!=cache_->lastSunAngle || atmo->sunAzimuth!=cache_->lastSunAzimuth)) {
            float elevation=glm::radians(atmo->sunAngle),azimuth=glm::radians(atmo->sunAzimuth);
            light->data.direction=-glm::vec3(std::cos(elevation)*std::sin(azimuth),std::sin(elevation),-std::cos(elevation)*std::cos(azimuth));
        } else {
            if(glm::dot(light->data.direction,light->data.direction)<1e-10f)throw std::invalid_argument("Sun needs a nonzero direction");
            auto sun=-glm::normalize(light->data.direction);atmo->sunAngle=glm::degrees(std::asin(glm::clamp(sun.y,-1.f,1.f)));
            atmo->sunAzimuth=glm::dot(glm::vec2(sun.x,sun.z),glm::vec2(sun.x,sun.z))<1e-10f?atmo->sunAzimuth:glm::degrees(std::atan2(sun.x,-sun.z));
        }
        cache_->sunAtmosphere=atmo;cache_->lastSunAngle=atmo->sunAngle;cache_->lastSunAzimuth=atmo->sunAzimuth;
    }
    for (const auto& l : directional) if (l && l->enabled) result.frame.lights.push_back({{0,0,0,0},glm::vec4(l->data.color,0),glm::vec4(l->data.direction,0)});
    for (const auto& l : points) if (l && l->enabled) {
        auto t = std::static_pointer_cast<Transform>(l->gameObject->GetComponent("Transform"));if (!t) throw std::invalid_argument("Renderer: light needs transform");
        result.frame.lights.push_back({glm::vec4(t->position,1),glm::vec4(l->data.color,0),{0,0,0,0}});
    }
    for (const auto& l : spots) if (l && l->enabled) {
        auto t = std::static_pointer_cast<Transform>(l->gameObject->GetComponent("Transform"));if (!t) throw std::invalid_argument("Renderer: light needs transform");
        result.frame.lights.push_back({glm::vec4(t->position,2),glm::vec4(l->data.color,l->data.cutOff),glm::vec4(l->data.direction,l->data.outerCutOff)});
    }
    result.frame.shadows=result.frame.ssao=result.frame.rsm=true;result.frame.inverseSquareLocalLights=true;
    result.frame.timeSeconds=timeOverride>=0?timeOverride:float(glfwGetTime());result.frame.taa=device_->computeLimits().maxStorageImages>0;result.frame.historyKey=reinterpret_cast<uint64_t>(scene.get())^(scene->revision()*0x9e3779b97f4a7c15ull);
    if(scene->sky){auto atmo=std::static_pointer_cast<Atmosphere>(scene->sky->GetComponent("Atmosphere"));if(atmo){result.frame.sky=true;result.frame.sunAngle=atmo->sunAngle;result.frame.sunAzimuth=atmo->sunAzimuth;result.frame.seaLevelMeters=atmo->seaLevelMeters;result.frame.multipleScattering=atmo->multipleScattering;result.frame.groundAlbedo=atmo->groundAlbedo;const auto& a=atmo->atmosphere;auto& p=result.frame.atmosphere;p.radii={a.solar_irradiance,a.sun_angular_radius,a.top_radius,a.bottom_radius};p.densities={a.HDensityRayleigh,a.HDensityMie,a.OzoneCenter,a.mie_g};p.rayleigh=glm::vec4(a.rayleigh_scattering,0);p.mie=glm::vec4(a.mie_scattering,0);p.extinction=glm::vec4(a.mie_extinction,0);p.absorption=glm::vec4(a.absorption_extinction,a.OzoneWidth);}}
    if(scene->terrain){auto terrain=std::static_pointer_cast<TerrainComponent>(scene->terrain->GetComponent("TerrainComponent"));if(terrain){
        if(!device_->computeLimits().maxStorageImages)throw std::invalid_argument("Terrain requires storage compute on this backend; OpenGL 4.1 migration is deferred");
        if(!cache_->terrain || cache_->terrain->source.lock()!=terrain){
            auto record=std::make_unique<Cache::TerrainRecord>();record->source=terrain;uint32_t width=terrain->heightWidth,height=terrain->heightHeight;
            if(!width || !height){auto tex=terrain->terrainMaterial?terrain->terrainMaterial->textures.find("heightMap"):decltype(terrain->terrainMaterial->textures.find("heightMap")){};if(!terrain->terrainMaterial || tex==terrain->terrainMaterial->textures.end())throw std::invalid_argument("Terrain lacks height field metadata");width=tex->second->width;height=tex->second->height;}
            std::vector<float> data(size_t(width)*height);if(!terrain->heightSourcePath.empty()){std::ifstream input(terrain->heightSourcePath,std::ios::binary);input.read(reinterpret_cast<char*>(data.data()),data.size()*4);if(!input)throw std::invalid_argument("Cannot read terrain height field: "+terrain->heightSourcePath);}else if(terrain->heightData && !terrain->initDone)std::copy_n(terrain->heightData,data.size(),data.begin());else throw std::invalid_argument("Terrain needs retained CPU height field or source path");
            record->gpu=std::make_shared<GpuTerrain>(device_,rhi::defaultShaderDirectory(),width,height,data);MaterialDesc material;material.parameters.factors={0,.85f,1,0};material.parameters.albedoAlpha={.3f,.45f,.2f,1};if(terrain->material){material.parameters=parameters(*terrain->material);for(uint32_t i=0;i<5;++i)material.images[i]=decode(*terrain->material,names[i]);}record->material=std::make_shared<GpuMaterial>(device_,material);
            auto grass=scene->terrain->GetComponent("Grass");if(grass){record->grass=std::make_shared<GpuGrass>(device_,rhi::defaultShaderDirectory(),record->gpu,terrain->model);MaterialDesc grassMaterial;grassMaterial.parameters.factors={0,1,1,0};grassMaterial.parameters.emissiveNormal.w=0;grassMaterial.extension.settings.w=1;grassMaterial.images[0]={1,2,{90,123,65,255,16,43,23,255}};record->grassMaterial=std::make_shared<GpuMaterial>(device_,grassMaterial);}
            cache_->terrain=std::move(record);
        }
        auto& record=*cache_->terrain;record.gpu->update(result.frame,terrain->model);result.packets.push_back({record.gpu->mesh(),record.material,terrain->model,0,terrain->polyMode==GL_LINE});if(record.grass){record.grass->update(terrain->model,result.frame.timeSeconds);result.packets.push_back({record.grass->mesh(),record.grassMaterial,glm::mat4(1)});}
    }}else cache_->terrain.reset();
    std::unordered_set<const Mesh*> usedMeshes;std::unordered_set<const Material*> usedMaterials;
    for (const auto& object : objects) if (object) {
        auto ocean=std::static_pointer_cast<Ocean>(object->GetComponent("Ocean"));if(ocean){OceanSurfaceSettings s;s.id=reinterpret_cast<uint64_t>(ocean.get());s.spectrum={uint32_t(ocean->fft_size),ocean->MeshLength,ocean->A,ocean->WindScale,ocean->Lambda,ocean->HeightScale,ocean->BubblesScale,ocean->BubblesThreshold,glm::vec2(ocean->WindAndSeed),ocean->seed};s.meshSize=uint32_t(ocean->MeshSize);s.seaLevel=ocean->seaLevel;s.timeScale=ocean->TimeScale;s.animate=ocean->animate;s.detailWaves=ocean->detailWaves;s.detailStrength=ocean->detailStrength;s.refraction=ocean->refraction;s.refractionStrength=ocean->refractionStrength;s.deepWaterDistance=ocean->deepWaterDistance;s.subsurfaceStrength=ocean->subsurfaceStrength;s.anisotropy=ocean->scatteringAnisotropy;s.absorption=ocean->absorption;s.scattering=ocean->scattering;s.fresnel=ocean->outer_FresnelScale;s.gloss=float(ocean->outer_Gloss);s.shallow=ocean->outer_OceanColorShallow;s.deep=ocean->outer_OceanColorDeep;s.foamColor=ocean->outer_BubblesColor;s.specular=ocean->outer_Specular;s.ambient=ocean->outer_ambient;result.frame.oceans.push_back(s);}

        auto filter = std::static_pointer_cast<MeshFilter>(object->GetComponent("MeshFilter"));auto t = std::static_pointer_cast<Transform>(object->GetComponent("Transform"));if (!filter || !t) continue;
        auto renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
        if (renderer && ((renderer->drawMode != GL_TRIANGLES && renderer->drawMode!=GL_PATCHES) || (renderer->polyMode != GL_FILL && renderer->polyMode != GL_LINE))) throw std::invalid_argument("Renderer: forward RHI path supports triangle meshes");
        for (const auto& mesh : filter->meshes) if (mesh) {
            usedMeshes.insert(mesh.get());auto found = cache_->meshes.find(mesh.get());
            if (found == cache_->meshes.end() || found->second.source.expired()) {
                std::vector<MeshVertex> vertices;std::vector<uint32_t> indices, remap(mesh->vertices.size(),UINT32_MAX);
                for (auto index : mesh->indices) {
                    if (index >= mesh->vertices.size()) throw std::invalid_argument("Renderer: invalid CPU mesh index");
                    if (remap[index] == UINT32_MAX) {
                        const auto& v = mesh->vertices[index];remap[index] = uint32_t(vertices.size());
                        vertices.push_back({v.Position,v.Normal,{v.TexCoords.x,1-v.TexCoords.y}});
                    }indices.push_back(remap[index]);
                }
                cache_->meshes[mesh.get()] = {mesh,std::make_shared<GpuMesh>(device_,vertices,indices)};
            }
            auto material = cache_->fallback;
            if (mesh->material) {
                const auto& m = mesh->material;usedMaterials.insert(m.get());const Cache::MaterialKey key{m.get(),renderer?renderer->shaderType:ShaderType::PBR};auto cached = cache_->materials.find(key);
                if (cached == cache_->materials.end() || cached->second.source.expired()) {
                    MaterialDesc desc;desc.parameters = parameters(*m);desc.extension=extension(key.second,*m);desc.special=specialMaps(*m);for (unsigned i = 0; i < 5; ++i) desc.images[i] = decode(*m,names[i]);
                    cache_->materials[key] = {m,std::make_shared<GpuMaterial>(device_,desc)};
                }
                material = cache_->materials.at(key).gpu;material->update(parameters(*m));material->updateExtension(extension(key.second,*m));
            }
            const auto model = glm::translate(glm::mat4(1),t->position)*glm::scale(glm::mat4(1),t->scale)*glm::eulerAngleYXZ(glm::radians(t->rotation.y),glm::radians(t->rotation.x),glm::radians(t->rotation.z));
            auto gpuMesh=cache_->meshes.at(mesh.get()).gpu;
            if(renderer && renderer->shaderType==ShaderType::PBR_TESS){
                const auto key=std::make_pair(object.get(),mesh.get());auto& record=cache_->subdivisions[key];
                if(record.source.lock()!=mesh){std::vector<MeshVertex> vertices;vertices.reserve(mesh->vertices.size());for(const auto& v:mesh->vertices)vertices.push_back({v.Position,v.Normal,{v.TexCoords.x,1-v.TexCoords.y}});record.source=mesh;record.gpu=std::make_shared<GpuSubdivision>(device_,rhi::defaultShaderDirectory(),vertices,mesh->indices,mesh->material?decode(*mesh->material,"material.height"):ImageRGBA8{});}
                float distance=std::numeric_limits<float>::max();for(const auto& vertex:mesh->vertices)distance=std::min(distance,glm::length(glm::vec3(result.frame.view*model*glm::vec4(vertex.Position,1))));const uint32_t level=uint32_t(std::ceil(glm::mix(10.f,1.f,glm::clamp((distance-.2f)/.8f,0.f,1.f))));record.gpu->update(level,material->extension().settings.y);gpuMesh=record.gpu->mesh();
            }
            result.packets.push_back({gpuMesh,material,model,reinterpret_cast<uint64_t>(object.get())^reinterpret_cast<uint64_t>(mesh.get()),renderer && renderer->polyMode==GL_LINE});
        }
    }
    for(auto it=cache_->subdivisions.begin();it!=cache_->subdivisions.end();)if(it->second.source.expired() || std::none_of(objects.begin(),objects.end(),[&](const auto& object){return object.get()==it->first.first;}))it=cache_->subdivisions.erase(it);else ++it;
    for (auto it = cache_->meshes.begin(); it != cache_->meshes.end();) if (!usedMeshes.count(it->first)) it = cache_->meshes.erase(it);else ++it;
    for (auto it = cache_->materials.begin(); it != cache_->materials.end();) if (!usedMaterials.count(it->first.first)) it = cache_->materials.erase(it);else ++it;
    return result;
}
}
