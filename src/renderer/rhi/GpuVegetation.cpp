#include "renderer/rhi/GpuVegetation.h"
#include "renderer/rhi/VegetationGeometry.h"
#include "renderer/rhi/TreeGeometry.h"
#include "renderer/rhi/GpuTreeWind.h"
namespace render {
namespace {
bool tree(PlantType t){return t==PlantType::Broadleaf||t==PlantType::Conifer||t==PlantType::Shrub;}
VegetationSettings placement(const VegetationSettings& base,const VegetationSpecies& species,uint32_t lod,uint32_t variant){
    auto p=base;p.species.clear();p.capacity=species.capacity;p.density=species.density*base.density;
    p.heightScale=species.height*base.heightScale;p.widthScale=species.width*base.widthScale;
    p.nearSpacing=species.spacing;p.farSpacing=species.farSpacing>0?species.farSpacing:species.spacing;p.distance=species.distance;
    p.denseRadius=species.denseRadius;p.sparseRadius=species.sparseRadius;p.farWidthScale=species.farWidthScale;
    p.fadeStart=species.distance*.85f;p.minimumNormalY=species.minimumNormalY;p.maxLod=5;
    p.placementSeed=species.seed;p.geometryLod=base.speciesLod?int(lod):-1;p.lodDistance=species.lodDistance;p.lodTransition=species.lodTransition;
    p.geometryVariant=variant;p.geometryVariants=species.variants;p.geometryRadius=1;p.stableLattice=true;
    if(tree(species.type)){p.environmentWind=true;p.windX=p.windZ=0;}
    // Main-camera rejection would remove off-screen shadow and reflection casters.
    p.frustumCull=false;p.validate();return p;
}
}
GpuVegetation::GpuVegetation(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,std::shared_ptr<GpuTerrain> terrain,const glm::mat4& model,const VegetationSettings& settings,std::shared_ptr<const ImageRGBA8> water,std::shared_ptr<const ImageRGBA8> exclusion):device_(d){
    settings.validate();
    for(const auto& species:settings.species){
        std::shared_ptr<GpuGrass> budget;
        for(uint32_t variant=0;variant<species.variants;variant++)for(uint32_t lod=0;lod<(settings.speciesLod?2u:1u);lod++){
            Batch b;b.species=species;b.lod=lod;b.variant=variant;
            if(tree(species.type)){
                uint32_t seed=species.seed+variant*104729u;auto high=treeGeometry(species.type,0,seed),g=treeGeometry(species.type,lod,seed);
                b.geometry=g.bark;b.leafGeometry=g.foliage;b.fullIndices=uint32_t(high.bark->indices.size()+high.foliage->indices.size());
                b.descriptor=treeMaterial(species.type,false);b.leafDescriptor=treeMaterial(species.type,true,lod);
            }else{
                b.geometry=plantGeometry(species.type,lod);b.fullIndices=uint32_t(plantGeometry(species.type,0)->indices.size());
                b.descriptor.sharedImages[0]=plantPalette(species.type);b.descriptor.parameters.factors={0,.8f,1,0};b.descriptor.parameters.emissiveNormal.w=0;b.descriptor.extension.settings.w=1;
            }
            b.generator=std::make_shared<GpuGrass>(d,dir,terrain,model,species.capacity,placement(settings,species,lod,variant),water,exclusion,b.geometry);
            if(budget)b.generator->sharePopulationBudget(budget);else budget=b.generator;
            b.material=std::make_shared<GpuMaterial>(d,b.descriptor);
            if(b.leafGeometry){b.leaves=GpuMesh::withGeneratedPoses(b.generator->mesh(),b.leafGeometry->vertices,b.leafGeometry->indices);b.leafMaterial=std::make_shared<GpuMaterial>(d,b.leafDescriptor);b.barkWind=std::make_shared<GpuTreeWind>(d,dir,b.generator->mesh(),b.geometry);b.leafWind=std::make_shared<GpuTreeWind>(d,dir,b.leaves,b.leafGeometry);}
            batches_.push_back(std::move(b));
        }
    }
}
bool GpuVegetation::compatible(const VegetationSettings& s)const{
    size_t index=0;for(const auto& species:s.species)for(uint32_t variant=0;variant<species.variants;variant++)for(uint32_t lod=0;lod<(s.speciesLod?2u:1u);lod++){
        if(index>=batches_.size())return false;const auto& b=batches_[index++];if(b.species.type!=species.type||b.species.capacity!=species.capacity||b.species.seed!=species.seed||b.lod!=lod||b.variant!=variant)return false;
    }return index==batches_.size();
}
void GpuVegetation::update(const glm::mat4& model,float seconds,const VegetationSettings& s){
    s.validate();if(!compatible(s))throw std::invalid_argument("Vegetation layout requires recreation");
    auto commands=device_->createCommandList();bool hasWind=false;
    size_t i=0;for(const auto& species:s.species)for(uint32_t variant=0;variant<species.variants;variant++)for(uint32_t lod=0;lod<(s.speciesLod?2u:1u);lod++){auto& b=batches_[i++];b.species=species;b.generator->update(model,seconds,placement(s,species,lod,variant));if(b.leaves){b.leaves->syncGeneratedPoses();b.wind=s.treeWind;if(s.environmentWind){b.wind.velocity={s.windX,s.windZ};b.wind.phase=s.windPhase;}float angle=variant*2.3999632f+float(species.seed%97)*.067f;auto velocity=b.wind.velocity;b.wind.velocity={std::cos(angle)*velocity.x-std::sin(angle)*velocity.y,std::sin(angle)*velocity.x+std::cos(angle)*velocity.y};b.wind.phase+=float(species.seed%97)*.031f+variant*1.37f;b.seconds=seconds;b.barkWind->record(commands,seconds,b.wind);b.leafWind->record(commands,seconds,b.wind);hasWind=true;}}
    if(hasWind)device_->submit(commands);
}
void GpuVegetation::append(std::vector<DrawPacket>& packets)const{for(const auto& b:batches_){packets.push_back({b.generator->mesh(),b.material,glm::mat4(1),0,false,b.generator->historyReliable()});if(b.leaves)packets.push_back({b.leaves,b.leafMaterial,glm::mat4(1),0,false,b.generator->historyReliable()});}}
std::vector<VegetationCaptureBatch> GpuVegetation::captureBatches()const{std::vector<VegetationCaptureBatch> out;for(const auto& b:batches_){out.push_back({{b.generator->mesh(),b.material,glm::mat4(1)},b.barkWind?freezeTreeWind(*b.geometry,b.seconds,b.wind):b.geometry,b.descriptor,false});if(b.leaves)out.push_back({{b.leaves,b.leafMaterial,glm::mat4(1)},freezeTreeWind(*b.leafGeometry,b.seconds,b.wind),b.leafDescriptor,true});}return out;}
std::vector<VegetationBatchStats> GpuVegetation::readStats(){std::vector<VegetationBatchStats> stats;for(auto& b:batches_){auto a=b.generator->readArguments();uint32_t indices=a.indexCount+(b.leaves?b.leaves->indexCount():0);stats.push_back({b.species.type,b.lod,a.instanceCount,a.instanceCount*(indices/3),a.instanceCount*(b.fullIndices/3),b.variant});}return stats;}
}
