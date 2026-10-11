#include "renderer/rhi/RasterInstanceBatcher.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/rhi/RasterVisibility.h"
#include <atomic>
#include <set>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
namespace render {
std::vector<DrawPacket> RasterInstanceBatcher::batch(const std::vector<DrawPacket>& packets){
    static std::atomic<uint64_t> nextId{uint64_t(1)<<61};
    stats_={};stats_.sourcePackets=packets.size();std::vector<DrawPacket> out;std::set<CacheKey> used;
    auto eligible=[](const DrawPacket& p){return p.mesh&&p.material&&!p.clusters&&!p.mesh->indirectBuffer()&&!p.mesh->previousVertices()&&!p.material->transparent()&&p.material->extension().lobes.w==0&&p.model[0].w==0&&p.model[1].w==0&&p.model[2].w==0&&p.model[3].w==1;};
    for(size_t first=0;first<packets.size();){
        if(!eligible(packets[first])){out.push_back(packets[first++]);continue;}
        size_t end=first;while(end<packets.size()&&eligible(packets[end]))++end;
        std::map<Key,std::vector<size_t>> groups;
        std::vector<Key> order;
        for(size_t i=first;i<end;++i){Key key{packets[i].mesh.get(),packets[i].material.get(),packets[i].wireframe};auto &g=groups[key];if(g.empty())order.push_back(key);g.push_back(i);}
        for(const auto& key:order){const auto& g=groups.at(key);const auto& p=packets[g.front()];
            // Bound each immutable pose block; cache every chunk by its first
            // member identity, including repeated geometry across barriers.
            for(size_t begin=0;begin<g.size();begin+=4096){const auto n=std::min(size_t(4096),g.size()-begin);
                if(n==1){out.push_back(packets[g[begin]]);continue;}
                std::vector<glm::mat4> poses;std::vector<uint64_t> ids;poses.reserve(n);ids.reserve(n);
                for(size_t j=0;j<n;++j){const auto& member=packets[g[begin+j]];poses.push_back(member.model);ids.push_back(member.id);}
                CacheKey cacheKey{key,ids.front()};const bool cacheable=ids.front()&&used.insert(cacheKey).second;
                Record transient;auto& record=cacheable?records_[cacheKey]:transient;
                if(record.base!=p.mesh||record.material!=p.material||record.ids!=ids||record.poses!=poses){
                    auto gpu=GpuMesh::withInstances(p.mesh,poses);if(!record.id)record.id=nextId.fetch_add(1);
                    record.base=p.mesh;record.material=p.material;record.ids=std::move(ids);record.poses=std::move(poses);record.batch=std::move(gpu);stats_.poseUploadBytes+=n*64;
                }else ++stats_.cacheHits;
                out.push_back({record.batch,p.material,glm::mat4(1),record.id,p.wireframe});stats_.instances+=n;
            }
        }
        first=end;
    }
    for(auto it=records_.begin();it!=records_.end();)if(!used.count(it->first))it=records_.erase(it);else ++it;
    stats_.outputPackets=out.size();return out;
}
void validateRasterInstanceBatching(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    if(!device->computeLimits().supported)return;
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const std::vector<MeshVertex> vertices{{{-1,-1,.4f},{0,0,1},{0,1}},{{1,-1,.4f},{0,0,1},{1,1}},{{1,1,.4f},{0,0,1},{1,0}},{{-1,1,.4f},{0,0,1},{0,0}}};
    auto mesh=std::make_shared<GpuMesh>(device,vertices,std::vector<uint32_t>{0,1,2,0,2,3});
    MaterialDesc desc;desc.parameters.factors={0,.5f,1,.5f};desc.parameters.emissiveNormal.w=0;
    desc.images[0]={2,2,{255,70,40,255,40,255,70,0,70,40,255,255,255,255,255,255}};
    auto material=std::make_shared<GpuMaterial>(device,desc);std::vector<DrawPacket> source;
    for(int i=0;i<4;++i){auto model=glm::translate(glm::mat4(1),glm::vec3(i&1?.45f:-.45f,i&2?.45f:-.45f,0))*glm::rotate(glm::mat4(1),.2f,glm::vec3(0,1,0))*glm::scale(glm::mat4(1),glm::vec3(i&1?-.3f:.3f,.25f,.8f));source.push_back({mesh,material,model,uint64_t(i+1)});}
    RasterInstanceBatcher batcher;auto batch=batcher.batch(source);
    check(batch.size()==1&&batch[0].mesh->instances()&&batch[0].mesh->vertexBuffer().value==mesh->vertexBuffer().value&&batcher.statistics().poseUploadBytes==256,"Ordinary instances did not batch / share geometry");
    auto original=batch[0].mesh;auto hot=batcher.batch(source);check(hot[0].mesh==original&&!batcher.statistics().poseUploadBytes&&batcher.statistics().cacheHits==1,"Static batch reuploaded poses");
    auto compare=[&](const FrameData& frame,const std::vector<DrawPacket>& plain,const std::vector<DrawPacket>& grouped){
        ForwardPbrRenderer renderer(device,directory,64,64,PbrPath::Scene);renderer.render(frame,plain);auto hdr=renderer.readHDR(),position=renderer.readGBuffer(0),normal=renderer.readGBuffer(1);renderer.render(frame,grouped);auto b=renderer.readHDR(),bp=renderer.readGBuffer(0),bn=renderer.readGBuffer(1);
        for(size_t i=0;i<hdr.size();++i){check(std::isfinite(b[i])&&std::abs(hdr[i]-b[i])<.008f,"Batched Raster HDR differs from individual draws");check(std::abs(position[i]-bp[i])<.002f&&std::abs(normal[i]-bn[i])<.002f,"Batched Raster transform / normal / cutout differs");}
    };
    FrameData frame;frame.lights.push_back({{0,0,0,0},{3,3,3,0},{0,0,-1,0}});compare(frame,source,batch);
    source[0].model[3].x+=.03f;auto edited=batcher.batch(source);check(edited[0].mesh!=original,"Changed batch mutated a live pose lease");
    std::vector<glm::mat4> old(4);device->readBuffer(original->instances(),0,256,old.data());check(old[0]!=source[0].model,"Old pose lease was overwritten");compare(frame,source,edited);
    auto transparent=desc;transparent.transparent=true;auto glass=std::make_shared<GpuMaterial>(device,transparent);
    auto barriers=source;barriers.insert(barriers.begin()+2,{mesh,glass,glm::mat4(1),99});auto split=batcher.batch(barriers);
    check(split.size()==3&&split[1].material==glass,"Transparent draw order barrier was lost");
    auto sss=desc;sss.extension.lobes.w=1;auto subsurface=std::make_shared<GpuMaterial>(device,sss);auto excluded=source;for(auto& p:excluded)p.material=subsurface;
    check(batcher.batch(excluded).size()==source.size(),"SSS back-depth draws entered unsupported instanced path");
    std::vector<DrawPacket> many(8192,source[0]);for(size_t i=0;i<many.size();++i)many[i].id=10000+i;
    auto chunks=batcher.batch(many);check(chunks.size()==2&&chunks[0].mesh->instanceCapacity()==4096,"Large instance group exceeded its pose block limit");
    batcher.batch(many);check(!batcher.statistics().poseUploadBytes&&batcher.statistics().cacheHits==2,"Large group reuploaded an unchanged chunk");
    auto singular=source;singular[0].model=glm::scale(glm::mat4(1),glm::vec3(0));bool rejected=false;try{batcher.batch(singular);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Singular member bypassed transform validation");
    rejected=false;try{GpuMesh::withInstances(mesh,{glm::scale(glm::mat4(1),glm::vec3(1e30f,1e20f,1e-20f))});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Nonfinite inverse normal matrix bypassed instance validation");
    batcher.batch({});check(!batcher.statistics().outputPackets,"Unused instance cache retained outputs");
    {
        auto cpu=std::make_shared<MeshPayload>();cpu->id=uint64_t(1)<<60;cpu->vertices=vertices;cpu->indices={0,1,2,0,2,3};RenderWorldSnapshot world;world.rasterInstanceBatching=true;
        for(size_t i=0;i<source.size();++i){SnapshotDraw draw;draw.objectId=i+1;draw.mesh=cpu;draw.model=source[i].model;world.draws.push_back(draw);}
        SceneAdapter adapter(device);auto accepted=adapter.resolve(world);auto changed=world;changed.draws[0].model[3].x+=.02f;
        adapter.beginPublication();auto candidate=adapter.resolve(changed);adapter.rollbackPublication();
        auto restored=adapter.resolve(world);check(restored.rasterBatches.poseUploadBytes==256&&restored.packets[0].mesh!=candidate.packets[0].mesh,"Rollback retained a rejected derived instance cache");
        std::vector<glm::mat4> poses(4);device->readBuffer(accepted.packets[0].mesh->instances(),0,256,poses.data());check(poses[0]==world.draws[0].model,"Publication rollback mutated the accepted pose lease");
    }
    {
        auto visible=source[0];FrameData f;f.viewportWidth=f.viewportHeight=64;
        check(rasterPacketVisible(visible,f),"Inside-frustum static object was rejected");
        for(auto offset:std::vector<glm::vec3>{{4,0,0},{-4,0,0},{0,4,0},{0,-4,0},{0,0,-4},{0,0,4}}){
            auto outside=visible;outside.model=glm::translate(glm::mat4(1),offset)*visible.model;
            check(!rasterPacketVisible(outside,f),"Homogeneous clip plane failed to reject a static object");
        }
        auto crossing=visible;crossing.model=glm::mat4(1);check(rasterPacketVisible(crossing,f),"Clip boundary intersection was rejected");
        auto guard=visible;guard.model=glm::translate(glm::mat4(1),glm::vec3(1.025f,0,0))*glm::scale(glm::mat4(1),glm::vec3(.001f));check(rasterPacketVisible(guard,f),"Two-pixel guard was lost");
        auto invalid=visible;invalid.model=glm::scale(glm::mat4(1),glm::vec3(0));invalid.model[3].x=100;
        check(rasterPacketVisible(invalid,f),"Culling hid an invalid transform from renderer validation");
        auto generated=visible;generated.mesh=original;generated.model[3].x=100;check(rasterPacketVisible(generated,f),"Generated geometry used static bounds");
        auto transparentOutside=visible;transparentOutside.material=glass;transparentOutside.model[3].x=100;check(rasterPacketVisible(transparentOutside,f),"Transparent segment barrier was removed");
        // Off-camera caster above the receiver. Auxiliary shadow geometry must
        // remain identical when the main view drops that instance.
        auto cpu=std::make_shared<MeshPayload>();cpu->id=(uint64_t(1)<<60)+1;cpu->vertices=vertices;cpu->indices={0,1,2,0,2,3};
        RenderWorldSnapshot world;world.frame=f;world.frame.shadows=true;world.frame.lights.push_back({{0,0,0,0},{3,3,3,0},{-.7f,0,-1,0}});
        for(size_t i=0;i<source.size();++i){SnapshotDraw draw;draw.objectId=i+1;draw.mesh=cpu;draw.model=source[i].model;world.draws.push_back(draw);}
        auto off=world.draws[0];off.objectId=99;off.model=glm::translate(glm::mat4(1),glm::vec3(1.2f,0,1.2f))*glm::scale(glm::mat4(1),glm::vec3(.3f));world.draws.push_back(off);
        SceneAdapter adapter(device);auto full=adapter.resolve(world);world.rasterInstanceBatching=world.rasterFrustumCulling=true;auto clipped=adapter.resolve(world);
        check(clipped.rasterCulledPackets==1&&clipped.rasterBatches.sourcePackets==5&&!clipped.auxiliaryPackets.empty(),"SceneAdapter did not separate main / auxiliary visibility");
        ForwardPbrRenderer renderer(device,directory,64,64,PbrPath::Scene);renderer.render(full.frame,full.packets);auto hdr=renderer.readHDR(),shadow=renderer.readShadowDepth();
        renderer.render(clipped.frame,clipped.packets,1,2.2f,&clipped.auxiliaryPackets);auto optimized=renderer.readHDR(),optimizedShadow=renderer.readShadowDepth();
        for(size_t i=0;i<hdr.size();++i)check(std::abs(hdr[i]-optimized[i])<.008f,"Camera culling changed the shadowed image");
        for(size_t i=0;i<shadow.size();++i)check(std::abs(shadow[i]-optimizedShadow[i])<.0001f,"Camera culling dropped an auxiliary caster");
        auto hotCulled=adapter.resolve(world);check(!hotCulled.rasterBatches.poseUploadBytes,"Unchanged camera / auxiliary batches reuploaded poses");
        world.frame.taa=true;auto temporal=adapter.resolve(world);check(!temporal.rasterCulledPackets&&temporal.auxiliaryPackets.empty(),"Camera culling bypassed TAA history fallback");
        world.frame.taa=false;world.frame.shadows=false;world.frame.oceans.push_back(OceanSurfaceSettings{});auto water=adapter.resolve(world);
        check(water.rasterCulledPackets==1&&!water.auxiliaryPackets.empty(),"Water reflection / refraction lost off-camera geometry");
        world.rasterFrustumCulling=false;auto disabled=adapter.resolve(world);check(!disabled.rasterCulledPackets&&disabled.auxiliaryPackets.empty(),"Disabled visibility retained a derived auxiliary list");
    }
    {
        auto geometry=std::make_shared<MeshPayload>();geometry->id=(uint64_t(1)<<60)+2;
        geometry->vertices=vertices;geometry->indices={0,1,2,0,2,3};
        auto images=std::make_shared<MaterialPayload>();images->id=geometry->id+1;
        images->images[0]=std::make_shared<const ImageRGBA8>(desc.images[0]);
        RenderWorldSnapshot world;world.frame=frame;
        std::vector<DrawPacket> independent;
        for(size_t i=0;i<source.size();++i){
            SnapshotDraw draw;draw.objectId=i+1;draw.mesh=geometry;draw.material=images;
            draw.model=source[i].model;draw.parameters=desc.parameters;draw.extension=desc.extension;
            draw.parameters.albedoAlpha.x=i%2?.25f:.8f;world.draws.push_back(draw);
            auto variant=desc;variant.parameters=draw.parameters;
            independent.push_back({mesh,std::make_shared<GpuMaterial>(device,variant),draw.model,draw.objectId});
        }
        SceneAdapter adapter(device);auto first=adapter.resolve(world);
        check(first.rasterMaterials.builds==2&&first.rasterMaterials.cacheHits==2&&first.rasterMaterials.liveVariants==2,
              "Instance overrides did not create exactly two material variants");
        check(first.packets[0].material==first.packets[2].material&&first.packets[1].material==first.packets[3].material&&first.packets[0].material!=first.packets[1].material,
              "Material variants aliased distinct overrides or split identical inputs");
        compare(frame,independent,first.packets);
        auto hot=adapter.resolve(world);
        check(!hot.rasterMaterials.builds&&hot.rasterMaterials.cacheHits==4&&hot.packets[0].material==first.packets[0].material,
              "Stable instance overrides rebuilt GPU materials");
        world.rasterInstanceBatching=true;auto grouped=adapter.resolve(world);
        check(grouped.packets.size()==2&&grouped.rasterBatches.instances==4,"Material variants prevented compatible instance batching");
        compare(frame,independent,grouped.packets);
        auto changed=world;changed.draws[0].parameters.albedoAlpha.x=.5f;
        adapter.beginPublication();auto candidate=adapter.resolve(changed);adapter.rollbackPublication();
        auto restored=adapter.resolve(world);
        check(candidate.rasterMaterials.builds==1&&restored.rasterMaterials.builds==0&&restored.rasterMaterials.liveVariants==2,
              "Publication rollback retained a candidate material variant");
        world.rasterInstanceBatching=false;auto plain=adapter.resolve(world);
        check(plain.packets[0].material==first.packets[0].material,"Rollback replaced an accepted material lease");
        auto revised=std::make_shared<MaterialPayload>(*images);++revised->revision;
        revised->images[0]=std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{30,90,255,255}});
        world.draws[0].material=revised;
        auto revisions=adapter.resolve(world);auto revisionsHot=adapter.resolve(world);
        check(revisions.rasterMaterials.builds==1&&revisions.rasterMaterials.liveVariants==3&&!revisionsHot.rasterMaterials.builds&&
              revisionsHot.packets[0].material!=revisionsHot.packets[2].material&&revisionsHot.packets[2].material==first.packets[2].material,
              "Concurrent immutable image revisions shared or thrashed a material variant");
        auto extended=world;extended.draws[0].extension.rainWetCells[15].w=1;
        extended.draws[1].pathTracingSpecularAnisotropy=.5f;
        auto extensionFrame=adapter.resolve(extended);auto extensionHot=adapter.resolve(extended);
        check(extensionFrame.rasterMaterials.builds==2&&!extensionHot.rasterMaterials.builds&&extensionHot.rasterMaterials.liveVariants==4,
              "Spatial wetness or shared GGX shape was omitted from material variant identity");
        for(int edit=0;edit<20;++edit){
            extended.draws[0].parameters.albedoAlpha.y=.2f+float(edit)*.01f;
            const auto edited=adapter.resolve(extended);
            check(edited.rasterMaterials.liveVariants==4&&edited.rasterMaterials.builds==1,"Unused material variants accumulated across edits");
        }
        auto empty=extended;empty.draws.clear();check(adapter.resolve(empty).rasterMaterials.liveVariants==0,"Removed draws retained material variants");
        auto many=world;many.draws.clear();
        for(uint64_t i=0;i<1024;++i){auto draw=world.draws[0];draw.objectId=i+1000;draw.parameters.albedoAlpha.x=float(i+1)/1024.f;many.draws.push_back(draw);}
        auto distinct=adapter.resolve(many);auto distinctHot=adapter.resolve(many);
        check(distinct.rasterMaterials.builds==1024&&distinctHot.rasterMaterials.liveVariants==1024&&!distinctHot.rasterMaterials.builds,
              "Large override set did not retain and reuse its distinct material variants");
        adapter.resolve(world);auto zero=world;zero.draws[0].extension.specularShape.y=-0.f;
        check(!adapter.resolve(zero).rasterMaterials.builds,"Signed zero split equal material variants into different hash buckets");
        // A replacement image revision keeps the accepted matching variant
        // visible while its bounded image upload remains incomplete.
        adapter.resolve(world);world.asynchronousStreaming=true;
        auto streaming=std::make_shared<MaterialPayload>(*revised);++streaming->revision;
        streaming->images[0]=std::make_shared<const ImageRGBA8>(ImageRGBA8{2048,2048,std::vector<uint8_t>(2048*2048*4,255)});
        auto visible=adapter.resolve(world).packets[0].material;world.draws[0].material=streaming;
        auto pending=adapter.resolve(world);
        check(pending.assetsPending&&pending.packets[0].material==visible,"Image streaming lost the accepted matching material variant");
        for(int step=0;step<100&&pending.assetsPending;++step)pending=adapter.resolve(world);
        check(!pending.assetsPending&&pending.packets[0].material!=visible&&!adapter.resolve(world).rasterMaterials.builds,
              "Streamed material revision did not publish and reuse its variant");
    }
    std::cout<<"Raster material variants: exact overrides / image revisions / spatial wetness / GGX shape / signed zero, 1024-variant hot reuse / batching / image streaming, rollback and edit eviction passed\n";
    std::cout<<"Raster visibility: six clip planes / guard, invalid-transform and generated fallback, independent shadow / water geometry, TAA fallback and hot reuse passed\n";
    std::cout<<"Raster preview instances: geometry sharing, pose reuse / immutable replacement, mirror / nonuniform normals / alpha and transparent / SSS barriers passed\n";
}
}
