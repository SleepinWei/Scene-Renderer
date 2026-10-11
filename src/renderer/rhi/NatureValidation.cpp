#include "renderer/rhi/GpuVegetation.h"
#include "renderer/rhi/GpuWaveParticles.h"
#include "renderer/rhi/VegetationGeometry.h"
#include "renderer/rhi/TreeGeometry.h"
#include "renderer/rhi/GpuTreeWind.h"
#include "renderer/rhi/GpuInstanceCuller.h"
#include "PT/ProceduralCapture.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <set>
#include <map>
#include <tuple>
namespace render {
namespace {void check(bool q,const char* why){if(!q)throw std::runtime_error(why);}}
void validateVegetation(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir){
    validateTerrainRhi(d,dir);
    {
        auto source=treeGeometry(PlantType::Broadleaf,0,71);TreeWindSettings wind;
        std::vector<DrawPacket> draws;std::vector<std::shared_ptr<GpuTreeWind>> animation;
        for(auto geometry:{source.bark,source.foliage}){auto mesh=std::make_shared<GpuMesh>(d,geometry->vertices,geometry->indices);auto material=std::make_shared<GpuMaterial>(d,treeMaterial(PlantType::Broadleaf,geometry==source.foliage));animation.push_back(std::make_shared<GpuTreeWind>(d,dir,mesh,geometry));draws.push_back({mesh,material,glm::scale(glm::mat4(1),glm::vec3(6))});}
        auto animate=[&](float seconds){auto commands=d->createCommandList();for(auto& a:animation)a->record(commands,seconds,wind);d->submit(commands);};
        animate(8);std::vector<std::vector<MeshVertex>> previous;
        float maxError=0;size_t part=0;
        for(auto geometry:{source.bark,source.foliage}){auto mesh=draws[part++].mesh;std::vector<MeshVertex> output(mesh->vertexCount());d->readBuffer(mesh->vertexBuffer(),0,output.size()*sizeof(MeshVertex),output.data());auto frozen=freezeTreeWind(*geometry,8,wind);
            for(size_t i=0;i<output.size();i++){maxError=std::max(maxError,glm::length(output[i].position-frozen->vertices[i].position));check(glm::length(output[i].normal-frozen->vertices[i].normal)<2e-5f,"Tree wind GPU normals disagree with frozen PT");check(std::abs(glm::length(output[i].normal)-1)<2e-5f,"Wind produces invalid normals");for(int c=0;c<3;c++)check(output[i].position[c]>=mesh->instanceBoundsMin()[c]&&output[i].position[c]<=mesh->instanceBoundsMax()[c],"Tree wind escapes caster bounds");}
            previous.push_back(std::move(output));}
        check(maxError<2e-5,"Tree wind GPU geometry disagrees with frozen PT");
        ForwardPbrRenderer renderer(d,dir,128,128,PbrPath::Scene);FrameData frame;frame.cameraPosition={0,3,10};frame.view=glm::lookAt(frame.cameraPosition,glm::vec3(0,3,0),glm::vec3(0,1,0));glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;frame.viewProjection=correction*glm::perspective(glm::radians(48.f),1.f,.1f,100.f)*frame.view;frame.viewportWidth=frame.viewportHeight=128;frame.taa=true;frame.shadows=true;LightData sun;sun.colorInner={4,4,4,0};sun.directionOuter={-.3f,-1,-.4f,0};frame.lights={sun};
        renderer.render(frame,draws);auto depth=renderer.readShadowDepth();animate(8+1.f/24);renderer.render(frame,draws);auto changedDepth=renderer.readShadowDepth();uint32_t changed=0;for(size_t i=0;i<depth.size();i++)changed+=std::abs(depth[i]-changedDepth[i])>1e-5f;check(changed>0,"Wind shadow does not follow deformed vertices");
        for(size_t part=0;part<draws.size();part++){auto mesh=draws[part].mesh;std::vector<MeshVertex> old(mesh->vertexCount());d->readBuffer(mesh->previousVertices(),0,old.size()*sizeof(MeshVertex),old.data());for(size_t i=0;i<old.size();i+=37)check(glm::length(old[i].position-previous[part][i].position)<1e-6,"Previous wind vertices lost");check(mesh->vertexHistoryReliable(),"Continuous wind rejects temporal history");}
        auto motion=renderer.readMotion();uint32_t moving=0;for(size_t i=0;i<motion.size();i+=4)if(motion[i+3]>0&&std::abs(motion[i])+std::abs(motion[i+1])>1e-5)moving++;check(moving>10,"Tree motion vectors omit branch/leaf deformation");
        wind.velocity={0,0};animate(9);part=0;for(auto geometry:{source.bark,source.foliage}){auto mesh=draws[part++].mesh;std::vector<MeshVertex> output(mesh->vertexCount());d->readBuffer(mesh->vertexBuffer(),0,output.size()*sizeof(MeshVertex),output.data());for(size_t i=0;i<output.size();i+=37)check(glm::length(output[i].position-geometry->vertices[i].position)<1e-6,"Zero wind does not restore rest geometry");}
        std::cout<<"Tree wind: CPU/GPU position max error "<<maxError<<", "<<moving<<" moving pixels, "<<changed<<" changed shadow pixels; previous geometry and zero wind PASS\n";
    }
    auto terrain=std::make_shared<GpuTerrain>(d,dir,32,32,std::vector<float>(1024,0),8192);
    FrameData f;f.cameraPosition={0,6,12};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;
    f.viewProjection=correction*glm::perspective(glm::radians(65.f),1.f,.1f,100.f)*f.view;f.viewportWidth=f.viewportHeight=128;
    auto model=glm::scale(glm::mat4(1),glm::vec3(16,1,16));terrain->update(f,model);
    VegetationSettings s;s.species=meadowSpecies();for(auto& v:s.species){v.variants=1;v.spacing=.8f;v.farSpacing=0;v.capacity=4096;v.lodDistance=8;v.lodTransition=4;}
    for(auto& v:s.species)if(uint32_t(v.type)>=5){v.capacity=128;v.spacing=3;}
    GpuVegetation plants(d,dir,terrain,model,s);plants.update(model,0,s);
    auto stats=plants.readStats();uint64_t triangles=0,full=0;uint32_t instances=0;
    for(auto& b:stats){triangles+=b.triangles;full+=b.fullDetailTriangles;instances+=b.instances;}
    check(instances>100&&triangles<full*.8,"Vegetation absent or distance LOD ineffective");
    std::vector<DrawPacket> packets;plants.append(packets);check(packets.size()==22,"Plant species batch count");
    ForwardPbrRenderer renderer(d,dir,128,128,PbrPath::Scene);renderer.render(f,packets);auto pixels=renderer.readGBuffer(0);uint32_t coverage=0;for(size_t i=3;i<pixels.size();i+=4)if(pixels[i]>.5f)coverage++;check(coverage>50,"Plant raster draw missing");
    {
        for(const auto& batch:plants.captureBatches())if(batch.foliage)
            check((batch.packet.material->extension().features.x&int(MaterialFeature::Foliage))!=0,"GPU material drops foliage feature");
        auto skyFrame=f;skyFrame.sky=true;skyFrame.taa=false;
        LightData sun;sun.colorInner={4.8f,4.5f,3.8f,0};sun.directionOuter={-.65f,-.5f,-.4f,0};skyFrame.lights={sun};
        auto leafMesh=std::make_shared<GpuMesh>(d,std::vector<MeshVertex>{{{-2,0,0},{0,0,1},{0,0}},{{2,0,0},{0,0,1},{1,0}},{{2,4,0},{0,0,1},{1,1}},{{-2,4,0},{0,0,1},{0,1}}},std::vector<uint32_t>{0,1,2,0,2,3});
        auto leafMaterial=std::make_shared<GpuMaterial>(d,treeMaterial(PlantType::Broadleaf,true));
        const std::vector<DrawPacket> cutout={{leafMesh,leafMaterial,glm::mat4(1)}};
        renderer.render(skyFrame,{});auto reference=renderer.readHDR();
        renderer.render(skyFrame,cutout);auto masked=renderer.readHDR(),positions=renderer.readGBuffer(0),effects=renderer.readGBuffer(4);
        uint32_t skyPixels=0,leafPixels=0;float error=0;
        for(size_t p=0;p<positions.size();p+=4){
            if(positions[p+3]==0){skyPixels++;for(int c=0;c<3;++c)error=std::max(error,std::abs(masked[p+c]-reference[p+c]));}
            else if(effects[p+3]>=2)leafPixels++;
        }
        check(skyPixels>50&&leafPixels>20,"Sky/foliage regression lacks raster coverage");
        check(error<1e-5f,"Foreground coverage corrupts solar disk derivatives at sky holes");
        std::cout<<"Sky holes match unobstructed sky: max HDR error "<<error<<"; "<<leafPixels<<" foliage pixels PASS\n";
    }
    // Each lattice root occurs in exactly one of the complementary LOD batches.
    std::vector<DrawPacket> rootPackets;for(auto b:plants.captureBatches())if(!b.foliage)rootPackets.push_back(b.packet);
    for(size_t i=0;i<rootPackets.size();i+=2){std::set<std::tuple<int,int>> roots;uint32_t sum=0;
        for(size_t j=i;j<i+2;j++){rhi::DrawIndexedIndirectArguments args;d->readBuffer(rootPackets[j].mesh->indirectBuffer(),0,sizeof(args),&args);std::vector<glm::mat4> poses(args.instanceCount);if(!poses.empty())d->readBuffer(rootPackets[j].mesh->instances(),0,poses.size()*64,poses.data());sum+=args.instanceCount;
            for(auto& p:poses)check(roots.emplace(int(std::round(p[3].x*10000)),int(std::round(p[3].z*10000))).second,"LOD draws duplicate roots");}
        check(sum<=s.species[i/2].capacity,"Species exceeds combined LOD instance capacity");}
    // A light view must retain casters independently of the main camera view.
    {
        auto base=std::make_shared<GpuMesh>(d,std::vector<MeshVertex>{{{-.2f,0,0},{0,0,1},{0,0}},{{.2f,0,0},{0,0,1},{1,0}},{{0,1,0},{0,0,1},{.5f,1}}},std::vector<uint32_t>{0,1,2});
        std::vector<glm::mat4> poses;for(float x:{0.f,2.f,12.f,-12.f})poses.push_back(glm::translate(glm::mat4(1),{x,0,0}));auto mesh=GpuMesh::withInstances(base,poses);
        FrameData lightView=f;lightView.viewProjection=correction*glm::ortho(-5.f,5.f,-5.f,5.f,.1f,30.f)*glm::lookAt(glm::vec3(0,10,0),glm::vec3(0),glm::vec3(0,0,1));
        std::vector<DrawPacket> caster={{mesh,packets[0].material,glm::mat4(1)}};GpuInstanceCuller culler(d,dir);auto commands=d->createCommandList();auto culled=culler.record(commands,lightView,caster,true);d->submit(commands);
        auto counts=culler.readStats();std::cout<<"Light-view culling: "<<counts[0]<<" -> "<<counts[1]<<" instances PASS\n";check(counts[0]==4&&counts[1]==2&&counts[3]==2,"Light-view instance compaction incorrect");
        std::vector<glm::mat4> kept(2);d->readBuffer(culled[0].poses,0,128,kept.data());for(auto pose:kept)check(pose[3].x>=0&&pose[3].x<=2,"Wrong casters survive light-view culling");
    }
    {
        auto shadowFrame=f;shadowFrame.shadows=true;LightData sun;sun.colorInner={4,4,4,0};sun.directionOuter={-.3f,-1,-.4f,0};shadowFrame.lights={sun};
        for(bool rsm:{false,true}){shadowFrame.rsm=rsm;shadowFrame.shadowSettings.instanceCulling=true;
        renderer.render(shadowFrame,packets);auto compactDepth=renderer.readShadowDepth();auto counts=renderer.readShadowInstanceStats();check(counts[0]>0&&counts[1]>0&&counts[1]<counts[0],"Shadow culling does not reduce caster population");
        shadowFrame.shadowSettings.instanceCulling=false;renderer.render(shadowFrame,packets);auto fullDepth=renderer.readShadowDepth();uint32_t shadowPixels=0;
        for(size_t i=0;i<fullDepth.size();i++){check(std::abs(fullDepth[i]-compactDepth[i])<1e-5f,"Shadow culling changes alpha-tested depth");shadowPixels+=fullDepth[i]<.999f;}
        check(shadowPixels>30,"Shadow depth comparison is empty");std::cout<<"Shadow alpha depth / RSM="<<rsm<<": culling on/off identical; "<<shadowPixels<<" occupied pixels PASS\n";}
    }
    {
        VegetationSettings variants;variants.species={s.species[5]};variants.species[0].variants=4;variants.species[0].capacity=64;variants.species[0].density=1;GpuVegetation forest(d,dir,terrain,model,variants);forest.update(model,0,variants);auto batches=forest.captureBatches();std::set<std::tuple<int,int>> roots;uint32_t total=0;
        for(size_t i=0;i<batches.size();i++){
            const auto& batch=batches[i];if(batch.foliage){check(batch.packet.mesh->instances().value==batches[i-1].packet.mesh->instances().value,"Bark/foliage poses diverge");continue;}
            rhi::DrawIndexedIndirectArguments args;d->readBuffer(batch.packet.mesh->indirectBuffer(),0,20,&args);std::vector<glm::mat4> poses(args.instanceCount);if(args.instanceCount)d->readBuffer(batch.packet.mesh->instances(),0,poses.size()*64,poses.data());total+=args.instanceCount;
            for(auto pose:poses)check(roots.emplace(int(std::round(pose[3].x*10000)),int(std::round(pose[3].z*10000))).second,"Variants duplicate roots");
        }check(total>10&&total<=64,"Tree variants exceed shared population budget");std::cout<<"Tree variants: "<<total<<" unique roots, 4 shapes, shared capacity 64 PASS\n";
    }
    std::map<std::tuple<size_t,int,int>,glm::mat4> oldPoses;
    for(size_t i=0;i<packets.size();i++){rhi::DrawIndexedIndirectArguments a;d->readBuffer(packets[i].mesh->indirectBuffer(),0,sizeof(a),&a);std::vector<glm::mat4> poses(a.instanceCount);if(!poses.empty())d->readBuffer(packets[i].mesh->instances(),0,poses.size()*64,poses.data());for(auto p:poses)oldPoses[{i,int(std::round(p[3].x*10000)),int(std::round(p[3].z*10000))}]=p;}
    terrain->update(f,model);auto before=stats;plants.update(model,1.f/24,s);stats=plants.readStats();
    packets.clear();plants.append(packets);bool bent=false;
    for(size_t i=0;i<packets.size();i++){check(packets[i].reliableGeneratedHistory,"Stable vegetation rejects all temporal history");rhi::DrawIndexedIndirectArguments a;d->readBuffer(packets[i].mesh->indirectBuffer(),0,sizeof(a),&a);std::vector<glm::mat4> poses(a.instanceCount),previous(a.instanceCount);if(poses.empty())continue;d->readBuffer(packets[i].mesh->instances(),0,poses.size()*64,poses.data());d->readBuffer(packets[i].mesh->previousInstances(),0,previous.size()*64,previous.data());
        for(size_t j=0;j<poses.size();j++){auto p=poses[j];auto old=oldPoses.at({i,int(std::round(p[3].x*10000)),int(std::round(p[3].z*10000))});for(int c=0;c<4;c++)check(glm::length(old[c]-previous[j][c])<1e-5,"Wind history is paired by draw index rather than stable root");if(glm::length(p[1]-previous[j][1])>.001f)bent=true;}}
    check(bent,"Wind motion history missing");f.taa=true;renderer.render(f,packets);auto motion=renderer.readMotion();uint32_t usable=0;for(size_t i=3;i<motion.size();i+=4)if(motion[i]>1.5f)usable++;check(usable>20,"Vegetation motion remains fully reactive");
    {
        RenderWorldSnapshot world;world.frame=f;auto source=std::make_shared<TerrainPayload>();source->id=15;source->capacity=8192;source->height=heightVirtualSource(32,32,std::vector<float>(1024,0));
        std::array<ImageRGBA8,5> images;for(auto& image:images)image={1,1,{255,255,255,255}};source->material=materialVirtualSource(images);source->grass=true;source->vegetation=s;
        SnapshotTerrain surface;surface.source=source;surface.model=model;world.terrain=surface;pt::CaptureOptions options;options.terrainGrid=16;options.textureExtent=64;options.grassLimit=16;
        auto frozen=pt::captureProcedural(world,d,options);size_t plants=0;
        for(auto& draw:frozen.draws)if(draw.pathTracingKind==2){plants++;check(draw.mesh&&!draw.mesh->indices.empty()&&draw.material&&draw.material->images[0],"PT vegetation loses geometry / palette");}
        check(plants>1&&!frozen.terrain,"PT does not freeze multiple plant species");
    }for(size_t i=0;i<stats.size();i++)check(stats[i].instances==before[i].instances,"Wind moves plant population");
    auto capped=s;for(auto& v:capped.species){v.capacity=32;v.density=1;}
    GpuVegetation limited(d,dir,terrain,model,capped);limited.update(model,0,capped);auto limitedStats=limited.readStats();
    for(size_t i=0;i<limitedStats.size();i+=2)check(limitedStats[i].instances+limitedStats[i+1].instances<=32,"Shared species budget exceeded under saturation");
    {
        auto densityFrame=f;densityFrame.cameraPosition={0,6,0};densityFrame.view=glm::lookAt(densityFrame.cameraPosition,glm::vec3(0,0,-1),glm::vec3(0,1,0));densityFrame.viewProjection=correction*glm::perspective(glm::radians(65.f),1.f,.1f,100.f)*densityFrame.view;terrain->update(densityFrame,model);
        VegetationSettings density;density.species={{PlantType::MeadowGrass,11,16384,1,.2f,.35f,.5f,30,8,4,.65f,1,3,8,2.5f}};
        GpuVegetation tapered(d,dir,terrain,model,density);tapered.update(model,0,density);std::vector<DrawPacket> draws;tapered.append(draws);
        std::map<std::pair<int,int>,glm::mat4> prior;uint32_t near=0,far=0;
        for(auto& draw:draws){rhi::DrawIndexedIndirectArguments a;d->readBuffer(draw.mesh->indirectBuffer(),0,sizeof(a),&a);std::vector<glm::mat4> poses(a.instanceCount);if(!poses.empty())d->readBuffer(draw.mesh->instances(),0,poses.size()*64,poses.data());for(auto p:poses){float r=glm::length(glm::vec2(p[3].x,p[3].z));near+=r<3;far+=r>=8&&r<12;prior[{int(std::round(p[3].x*10000)),int(std::round(p[3].z*10000))}]=p;}}
        double nearDensity=near/(3.141592653589793*9),farDensity=far/(3.141592653589793*(144-64));
        check(nearDensity>20&&farDensity>.5&&farDensity<nearDensity*.1,"Species near/far density does not follow spacing squared");
        densityFrame.cameraPosition.x=1;densityFrame.view=glm::lookAt(densityFrame.cameraPosition,glm::vec3(1,0,-1),glm::vec3(0,1,0));densityFrame.viewProjection=correction*glm::perspective(glm::radians(65.f),1.f,.1f,100.f)*densityFrame.view;terrain->update(densityFrame,model);tapered.update(model,0,density);draws.clear();tapered.append(draws);uint32_t matched=0,changed=0;
        for(auto& draw:draws){rhi::DrawIndexedIndirectArguments a;d->readBuffer(draw.mesh->indirectBuffer(),0,sizeof(a),&a);std::vector<glm::mat4> poses(a.instanceCount),old(a.instanceCount);if(poses.empty())continue;d->readBuffer(draw.mesh->instances(),0,poses.size()*64,poses.data());d->readBuffer(draw.mesh->previousInstances(),0,old.size()*64,old.data());for(size_t i=0;i<poses.size();i++){auto p=poses[i];auto found=prior.find({int(std::round(p[3].x*10000)),int(std::round(p[3].z*10000))});if(found==prior.end())continue;matched++;for(int c=0;c<4;c++)check(glm::length(old[i][c]-found->second[c])<2e-5f,"Coverage width history uses the current camera");changed+=glm::length(p[0]-old[i][0])>.001f;}}
        check(matched>100&&changed>20,"Coverage/history camera test did not exercise a width transition");
        density.species[0].spacing=.25f;tapered.update(model,0,density);draws.clear();tapered.append(draws);for(const auto& draw:draws)check(!draw.reliableGeneratedHistory,"Edited root lattice reuses unrelated history");
        std::cout<<"Distance density: "<<nearDensity<<" clumps/m2 near, "<<farDensity<<" far; moving-camera coverage history PASS\n";
    }
    s.density=0;plants.update(model,0,s);for(auto b:plants.readStats())check(b.instances==0,"Density zero leaves plants");
    std::cout<<"Vegetation GPU: "<<instances<<" instances, "<<triangles<<" vs "<<full<<" full-detail triangles; unique LOD roots and raster coverage PASS\n";
}
void validateWaveParticles(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir){
    Resources resources(d);auto empty=resources.texture({1,1,rhi::Format::RGBA32Float,rhi::TextureUsage::Sampled|rhi::TextureUsage::CopyDestination,"Particle validation shore"});auto emptyView=resources.view(empty);glm::vec4 q(2,1,3,-2);d->writeTextureFloat(empty,&q.x,16);
    WaveParticleSettings s;s.resolution=64;s.length=16;s.impulses={{1,0,{0,0},{1,0},1,1.5f,6.2831853f}};
    GpuWaveParticles gpu(d,dir,64);glm::vec4 patch(-8,-8,16,.25f);gpu.simulate(.5,s,patch,emptyView,false);auto result=gpu.read();double maxError=0;float maximum=0;
    for(uint32_t y=0;y<64;y++)for(uint32_t x=0;x<64;x++){size_t at=(y*64+x)*4;auto expected=gpu.simulation().sample({-8+(x+.5f)*.25f,-8+(y+.5f)*.25f}).x;maxError=std::max(maxError,double(std::abs(result[at+1]-expected)));maximum=std::max(maximum,result[at+1]);check(result[at]==0&&result[at+3]==0,"Wave-only field contains invented water depth");}
    check(maxError<1e-5&&maximum>.01f,"Binned GPU wave kernel mismatch");
    auto old=result;gpu.simulate(.5,s,patch,emptyView,false);check(gpu.read(true)==old,"Unchanged wave time leaves stale previous field");
    gpu.simulate(.7,s,{0,-8,16,.25f},emptyView,true);check(gpu.previousPatch()==patch,"Camera scroll loses previous wave patch");check(gpu.read(true)==old,"Camera scroll corrupts previous wave texels");result=gpu.read();for(size_t at=0;at<result.size();at+=4)check(result[at]==2&&result[at+3]==-2,"Combined field corrupts shallow-water depth/bed");
    GpuWaveParticles partial(d,dir,40);partial.simulate(.5,s,{-8,-8,16,.4f},emptyView,false);auto partialField=partial.read();
    for(uint32_t y=0;y<40;y++)for(uint32_t x=0;x<40;x++)check(std::abs(partialField[(y*40+x)*4+1]-partial.simulation().sample({-8+(x+.5f)*.4f,-8+(y+.5f)*.4f}).x)<1e-5,"Partial tile wave bin mismatch");
    check(gpu.binReferences()<gpu.simulation().particles().size()*16,"Wave bins degenerated into all-particles-per-pixel reconstruction");
    std::cout<<"Wave particles GPU: "<<gpu.simulation().particles().size()<<" particles, "<<gpu.binReferences()<<" tile references; CPU/GPU max error "<<maxError<<", scroll and shore composition PASS\n";
}
}
