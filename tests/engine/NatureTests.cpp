#include "engine/WaveParticles.h"
#include "renderer/rhi/VegetationGeometry.h"
#include "renderer/rhi/TreeGeometry.h"
#include "renderer/rhi/FoliageMaterial.h"
#include "renderer/rhi/ProceduralSand.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "component/VegetationSettings.h"
#include <iostream>
#include <numeric>
static void check(bool q,const char* why){if(!q)throw std::runtime_error(why);}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid settings accepted");}
int main(){try{
    for(uint32_t type=0;type<8;type++){
        auto high=render::plantGeometry(PlantType(type),0),low=render::plantGeometry(PlantType(type),1);
        check(high->indices.size()>low->indices.size()&&!low->indices.empty(),"Plant LOD does not reduce geometry");
        for(auto mesh:{high,low}){for(auto i:mesh->indices)check(i<mesh->vertices.size(),"Plant index out of range");
            for(auto v:mesh->vertices){check(std::isfinite(v.position.x)&&std::isfinite(v.position.y)&&std::isfinite(v.position.z)&&std::abs(glm::length(v.normal)-1)<1e-5,"Invalid plant mesh");check(v.position.y>=-.001f&&v.position.y<=1.001f,"Plant height normalization");}}
    }
    auto oak=render::treeGeometry(PlantType::Broadleaf,0,71),same=render::treeGeometry(PlantType::Broadleaf,0,71),otherOak=render::treeGeometry(PlantType::Broadleaf,0,72),farOak=render::treeGeometry(PlantType::Broadleaf,1,71);
    check(oak.bark==same.bark&&oak.foliage==same.foliage,"Seeded tree cache unstable");
    check(oak.branches>200&&oak.leaves>1000&&farOak.leaves==oak.leaves,"Tree hierarchy missing or LOD uses another skeleton");
    check(oak.bark->vertices[100].position!=otherOak.bark->vertices[100].position,"Tree seeds do not change shape");
    {
        render::TreeWindSettings wind;double moved=0,flutter=0;
        for(auto mesh:{oak.bark,oak.foliage}){check(mesh->treeWind.size()==mesh->vertices.size(),"Tree wind metadata missing");
            for(size_t i=0;i<mesh->vertices.size();i++){auto p=mesh->vertices[i].position,n=mesh->vertices[i].normal,rest=p;render::treeWindDeform(p,n,mesh->treeWind[i],wind,8);
                check(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(glm::length(n)-1)<1e-5,"Tree wind invalid normal");
                if(std::abs(rest.y)<1e-6)check(glm::length(p-rest)<1e-6,"Wind detaches tree roots");moved+=glm::length(p-rest);
                auto zero=wind;zero.velocity={0,0};auto q=rest,m=mesh->vertices[i].normal;render::treeWindDeform(q,m,mesh->treeWind[i],zero,19);check(glm::length(q-rest)==0,"Zero wind changes tree");
                auto noFlutter=wind;noFlutter.flutter=0;q=rest;m=mesh->vertices[i].normal;render::treeWindDeform(q,m,mesh->treeWind[i],noFlutter,8);flutter+=glm::length(q-p);
            }
        }
        check(moved>10&&flutter>1,"Tree lacks branch motion or independent leaf flutter");
        rejects([&]{auto w=wind;w.flutter=3;w.validate();});
    }
    check(farOak.bark->indices.size()+farOak.foliage->indices.size()<(oak.bark->indices.size()+oak.foliage->indices.size())*.5,"Tree cluster LOD ineffective");
    auto leaf=render::treeMaterial(PlantType::Broadleaf,true);check((leaf.extension.features.x&32)!=0&&leaf.parameters.factors.w>0&&leaf.sharedImages[1]&&leaf.sharedImages[3],"Leaf material channels missing");
    uint32_t holes=0,solid=0;for(size_t i=3;i<leaf.sharedImages[0]->pixels.size();i+=4){holes+=leaf.sharedImages[0]->pixels[i]<20;solid+=leaf.sharedImages[0]->pixels[i]>250;}check(holes>1000&&solid>1000,"Leaf shadow alpha map has no silhouette");
    render::SnapshotDraw detached;auto payload=std::make_shared<render::MaterialPayload>();payload->images=leaf.sharedImages;payload->special=leaf.sharedSpecial;detached.material=payload;detached.parameters=leaf.parameters;detached.extension=leaf.extension;render::configureFoliagePathTracing(detached);
    check(detached.pathTracingBsdfModel==3&&detached.pathTracingTransmissionTexture&&detached.material->images[0]!=payload->images[0],"PT foliage bridge mutates source material or loses transmission");
    for(size_t i=0;i<detached.pathTracingTransmissionTexture->pixels.size();i+=4){for(int c=0;c<3;c++){float r=std::pow(detached.material->images[0]->pixels[i+c]/255.f,2.2f),t=std::pow(detached.pathTracingTransmissionTexture->pixels[i+c]/255.f,2.2f);check(r+t<=1.01f,"Leaf reflection/transmission violates energy bound");}check(detached.material->images[0]->pixels[i+3]==payload->images[0]->pixels[i+3],"PT foliage bridge changes alpha silhouette");}
    auto prior=detached.material;render::configureFoliagePathTracing(detached);check(prior==detached.material,"PT foliage bridge applied twice");
    VegetationSettings plants;plants.species=meadowSpecies();plants.validate();rejects([&]{auto p=plants;p.species[0].spacing=0;p.validate();});
    rejects([&]{auto p=plants;p.species[0].farSpacing=.1f;p.validate();});
    rejects([&]{auto p=plants;p.species[0].farWidthScale=5;p.validate();});
    render::ProceduralSand sand,other(174);double rippleVariance=0,seedDifference=0;
    for(int y=-16;y<16;y++)for(int x=-16;x<16;x++){
        glm::vec2 at(x*.173f,y*.219f);auto s=sand.sample(at),tile=sand.sample(at+glm::vec2(128,128));
        check(std::abs(s.height-tile.height)<2e-6f&&glm::length(s.gradient-tile.gradient)<3e-5f&&glm::length(s.albedo-tile.albedo)<3e-6f,"Sand tile seam");
        float dx=.001f;glm::vec2 gradient((sand.sample(at+glm::vec2(dx,0)).height-sand.sample(at-glm::vec2(dx,0)).height)/(2*dx),(sand.sample(at+glm::vec2(0,dx)).height-sand.sample(at-glm::vec2(0,dx)).height)/(2*dx));
        check(glm::length(gradient-s.gradient)<4e-5f,"Sand normal does not derive from relief");
        check(glm::length(s.gradient)<.25f&&s.albedo.x>.65f&&s.albedo.x<.85f,"Sand contrast or slope unbounded");
        rippleVariance+=s.height*s.height;seedDifference+=std::abs(s.height-other.sample(at).height);
    }
    check(rippleVariance>1e-4&&seedDifference>.1,"Sand lacks seeded ripple variation");
    engine::WaveParticle p{{0,0},{1,0},1,2,0};
    check(std::abs(engine::WaveParticles::kernel(p,{0,0}).x-1)<1e-6,"Compact kernel centre");
    check(glm::length(engine::WaveParticles::kernel(p,{2,0}))==0,"Kernel support or gradient discontinuity");
    auto k=engine::WaveParticles::kernel(p,{.7f,.3f});float dx=.001f;
    float derivative=(engine::WaveParticles::kernel(p,{.7f+dx,.3f}).x-engine::WaveParticles::kernel(p,{.7f-dx,.3f}).x)/(2*dx);
    check(std::abs(derivative-k.y)<.0001f,"Kernel derivative");
    WaveParticleSettings s;s.damping=0;s.minAmplitude=1e-8f;s.impulses={{1,0,{0,0},{1,0},1,1,6.2831853f}};
    engine::WaveParticles a,b;a.advance(2,s);for(int i=0;i<240;i++)b.advance((i+1)/120.,s);
    check(a.particles().size()>16&&a.particles().size()==b.particles().size(),"Subdivision or frame-rate independence");
    float amplitude=0;for(size_t i=0;i<a.particles().size();i++){amplitude+=a.particles()[i].amplitude;check(glm::length(a.particles()[i].position-b.particles()[i].position)<1e-5,"Wave state depends on render rate");}
    check(std::abs(amplitude-1)<.00001f,"Subdivision loses integrated kernel amplitude");
    auto before=a.sample({1,0});a.advance(2,s);check(glm::length(before-a.sample({1,0}))==0,"Repeated time duplicates sources");
    a.advance(1,s);engine::WaveParticles rewind;rewind.advance(1,s);check(a.particles().size()==rewind.particles().size()&&glm::length(a.sample({1,0})-rewind.sample({1,0}))<1e-6,"Rewind is not deterministic");
    s.reflectBounds=true;s.boundaryMin={-1,-1};s.boundaryMax={1,1};s.impulses={{1,0,{.9f,0},{1,0},1,.5f,.01f}};a.clear();a.advance(.2,s);check(a.particles()[0].direction.x<0&&a.particles()[0].position.x<1,"Boundary reflection");
    s.reflectBounds=false;s.impulses={{1,0,{0,0},{1,0},1,1,6.2831853f}};s.capacity=32;a.clear();a.advance(3,s);check(a.particles().size()<=32&&a.deferredSplits()>0,"Bounded split policy");
    s.capacity=8192;s.damping=.5f;s.impulses={{1,1,{0,0},{1,0},1,1,6.2831853f}};a.clear();a.advance(.5,s);check(a.particles().empty(),"Future impulse applied early");a.advance(1.5,s);amplitude=0;for(auto p:a.particles())amplitude+=p.amplitude;check(std::abs(amplitude-std::exp(-.25f))<1e-5,"Wave damping not exponential");
    s.impulses.clear();s.emitters={{{0,0},{1,0},0,1,.1f,.2f,1}};a.clear();a.advance(.5,s);amplitude=0;for(auto p:a.particles())amplitude+=p.amplitude;check(!a.particles().empty()&&std::abs(amplitude)<1e-5,"Moving emitter wake lacks signed displacement pair");
    s.emitters.clear();s.impulses={{1,0,{0,0},{1,0},1,1,6.2831853f}};
    s.impulses.push_back(s.impulses[0]);rejects([&]{s.validate();});s.impulses.pop_back();s.speed=NAN;rejects([&]{s.validate();});
    std::cout<<"Nature geometry, seeded sand / gradients / seams, compact kernel, splitting, budgets, reflection, clocks: PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
