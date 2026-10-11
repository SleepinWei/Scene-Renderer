#include "engine/Environment.h"
#include "engine/FixedStepClock.h"
#include "engine/BoundedQueue.h"
#include "renderer/rhi/EnvironmentBridge.h"
#include "PT/SceneIdentity.h"
#include <json/json.hpp>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <future>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include "renderer/rhi/MaterialWetness.h"

using namespace engine;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> static void rejects(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid operation accepted");}
static EnvironmentConfig config(){return {"classic:mountain-lake",1.0/60,8,{35,20}};}
int main(){try{
    {
        Environment a(config()),b(config());a.setEnabled(true);b.setEnabled(true);
        a.enqueueWind({-20,4},30);b.enqueueWind({-20,4},30);
        a.enqueueRain(36./3600000,30);b.enqueueRain(36./3600000,30);
        a.enqueueSolar({12,85},90);b.enqueueSolar({12,85},90);
        FixedStepClock fast,slow;
        for(int i=0;i<240;++i)fast.advance(1.0/120,[&](double){a.step();});
        for(int i=0;i<60;++i)slow.advance(1.0/30,[&](double){b.step();});
        check(a.checkpoint()==b.checkpoint()&&a.snapshot().tick==120,"State depends on rendered frame rate");
        fast.setPaused(true);auto before=a.checkpoint();fast.advance(1, [&](double){a.step();});
        check(a.checkpoint()==before,"Pause changed environment");a.step();check(a.snapshot().tick==121,"Single step failed");
        a.setEnabled(false);before=a.checkpoint();a.step();check(a.checkpoint()==before,"Disabled environment advanced");
    }
    {
        Environment e(config());e.setEnabled(true);BoundedQueue<EnvironmentSnapshot> pictures(1);
        auto accepted=e.enqueueSolar({15,-25},3);check(pictures.tryPush(e.snapshot()),"Snapshot submission failed");
        check(e.requestedSolar().elevationDegrees==15&&e.snapshot().solar.elevationDegrees==35,
              "Paused/future slider intent was lost or applied before its tick");
        for(int i=0;i<3;++i){e.step();check(!pictures.tryPush(e.snapshot()),"Queue should skip pictures");}
        check(e.snapshot().appliedSequence==accepted&&e.snapshot().solar.azimuthDegrees==-25,
              "Skipped render snapshots lost accepted event");
        pictures.pop();check(pictures.tryPush(e.snapshot()),"Latest snapshot unavailable");
        check(pictures.pop()->appliedSequence==accepted,"Published acknowledgement differs from CPU state");
    }
    {
        Environment e(config());e.setEnabled(true);
        for(size_t i=0;i<Environment::eventCapacity;++i)e.enqueueSolar({double(i),0},2);
        auto before=e.checkpoint();rejects([&]{e.enqueueSolar({1,0},2);});
        check(e.checkpoint()==before,"Rejected full-queue event mutated state");
        rejects([&]{e.enqueueSolar({NAN,0},2);});rejects([&]{e.enqueueSolar({0,0},0);});
        check(e.checkpoint()==before,"Rejected event changed state");
        e.step();e.step();check(e.snapshot().appliedSequence==Environment::eventCapacity&&e.snapshot().solar.elevationDegrees==63,
            "Same-tick events are not applied in submission order");
        e.enqueueSolar({2,3},10);before=e.checkpoint();rejects([&]{e.enqueueSolar({4,5},9);});
        check(e.checkpoint()==before,"Unordered event changed queue");
    }
    {
        Environment source(config()),restored(config());source.setEnabled(true);
        source.enqueueSolar({0,70},10);source.enqueueWind({-5,25},10);source.enqueueSolar({20,-40},20);source.enqueueCoverage(.3,20);
        for(int i=0;i<7;++i)source.step();const auto saved=source.checkpoint();
        const auto epoch=restored.snapshot().epoch;restored.restore(saved);
        check(restored.snapshot().epoch>epoch&&restored.checkpoint()==saved,"Checkpoint failed to restore state/events");
        for(int i=0;i<30;++i){source.step();restored.step();}
        check(source.checkpoint()==restored.checkpoint(),"Restored future events diverged");
        auto before=restored.checkpoint();auto j=nlohmann::json::parse(saved);
        auto bad=[&](auto edit){auto broken=j;edit(broken);try{rejects([&]{restored.restore(broken.dump());});}catch(...){throw std::runtime_error("Checkpoint rejection failed: "+broken.dump());}check(restored.checkpoint()==before,"Failed restore partially mutated state");};
        bad([](auto& x){x["scene_key"]="other";});bad([](auto& x){x["schema_version"]=6;});
        bad([](auto& x){x["tick"]=-1;});bad([](auto& x){x["tick"]=7.5;});
        bad([](auto& x){x["events"][0]["sequence"]=4;});bad([](auto& x){x["events"][1]["tick"]=9;});
        bad([](auto& x){x["events"].erase(0);});bad([](auto& x){x["solar"]["elevation_degrees"]=100;});
        bad([](auto& x){x["step_seconds"]=.01;});bad([](auto& x){x["scope"]="gpu-water";});
        rejects([&]{restored.restore(std::string(300000,'x'));});rejects([&]{restored.restore("{");});
        check(restored.checkpoint()==before,"Malformed restore changed state");
        const auto root=std::filesystem::temp_directory_path()/"scene-renderer-environment-test";
        std::filesystem::create_directories(root);const auto path=(root/"checkpoint.json").string();
        source.save(path);restored.load(path);check(source.checkpoint()==restored.checkpoint(),"File checkpoint roundtrip failed");
        source.step();source.save(path);restored.load(path);check(source.checkpoint()==restored.checkpoint(),"Atomic checkpoint replacement failed");
        std::filesystem::remove(path);
        check(std::async(std::launch::async,[&]{try{restored.snapshot();return false;}catch(const std::logic_error&){return true;}}).get(),
              "Foreign thread read live environment");
    }
    {
        render::RenderWorldSnapshot original;original.frame.historyKey=17;original.frame.timeSeconds=8;
        original.frame.sky=true;original.frame.sunAngle=30;original.frame.sunAzimuth=-15;
        original.frame.lights={{{0,0,0,0},{3,4,5,0},{0,-1,0,0}},{{0,2,0,1},{1,1,1,0},{0,-1,0,.5}}};
        auto c=render::environmentConfig(original,"classic:mountain-lake");
        check(std::abs(c.initialSolar.elevationDegrees-90)<1e-5,"Initial direction light was not authoritative");
        Environment e(c);auto disabled=original;render::applyEnvironment(disabled,e.snapshot());
        check(disabled.frame.historyKey==17&&disabled.frame.timeSeconds==8&&!disabled.environment,"Disabled bridge changed legacy scene");
        e.setEnabled(true);e.enqueueSolar({30,45},1);e.step();auto world=original;render::applyEnvironment(world,e.snapshot());
        const glm::vec3 expected{.6123724f,.5f,-.6123724f};
        check(glm::length(-glm::vec3(world.frame.lights[0].directionOuter)-expected)<1e-5,"Solar direction convention differs");
        check(world.frame.sunAngle==30&&world.frame.sunAzimuth==45&&world.frame.lights[0].colorInner==original.frame.lights[0].colorInner,
              "Bridge changed irradiance or atmosphere disagrees with sun");
        check(world.frame.lights[1].directionOuter==original.frame.lights[1].directionOuter,"Bridge modified local light");
        const auto key=world.frame.historyKey;const auto identity=pt::sceneIdentity(world);
        e.enqueueSolar({20,45},2);e.step();auto next=original;render::applyEnvironment(next,e.snapshot());
        check(next.frame.historyKey==key&&!(pt::sceneIdentity(next)==identity),"Continuous revision cleared all history or PT failed to see sun change");
        const auto saved=e.checkpoint();e.restore(saved);auto restored=original;render::applyEnvironment(restored,e.snapshot());
        check(restored.frame.historyKey!=key,"Restore did not invalidate image history");
        check(restored.environment->tick==2&&restored.environment->appliedSequence==2,"Detached state metadata missing");
    }
    {
        auto c=config();c.initialSeconds=0;c.initialWind={0,0};
        Environment e(c);e.setEnabled(true);e.enqueueWind({20,-10},1);e.enqueueSolar({25,45},1);
        check(e.requestedWind().x==20&&e.requestedSolar().elevationDegrees==25,"Mixed event intent was lost");
        check(e.snapshot().cloudX==0&&e.snapshot().wind.x==0,"Wind applied before its tick");
        for(int i=0;i<120;++i)e.step();auto state=e.snapshot();
        check(std::abs(state.wind.x-20*(1-std::exp(-2)))<1e-11,"Wind response differs from exact integral");
        check(std::abs(state.cloudX-20*(1+std::exp(-2)))<1e-10&&std::abs(state.cloudZ+10*(1+std::exp(-2)))<1e-10,
              "Cloud displacement rewrites history or depends on frame rate");
        Environment resumed(c);resumed.restore(e.checkpoint());
        e.enqueueWind({-15,0},121);resumed.enqueueWind({-15,0},121);
        const auto waveDestination=state.waveTo;e.step();resumed.step();state=e.snapshot();
        check(state.waveTo.x==waveDestination.x&&state.waveFrom.x==0,"New target interrupted a live wave blend");
        check(std::abs(state.cloudX-20*(1+std::exp(-2)))<.34,"Wind event teleported cloud position");
        for(int i=0;i<700;++i){e.step();resumed.step();}
        check(e.checkpoint()==resumed.checkpoint(),"Mid-transition checkpoint lost cloud/grass/wave continuity");
        check(e.snapshot().waveFrom.x==20&&e.snapshot().waveTo.x==-15,"Deferred wave target never started");
        const auto saved=e.checkpoint();auto j=nlohmann::json::parse(saved);
        for(const auto& field:{"wind","wave_from","wave_to","wind_target"}){
            auto bad=j;bad[field]["x"]=101;rejects([&]{e.restore(bad.dump());});check(e.checkpoint()==saved,"Invalid wind partially restored");
        }
        auto bad=j;bad["wave_elapsed"]=9;rejects([&]{e.restore(bad.dump());});
        bad=j;bad["cloud_x"]=1e11;rejects([&]{e.restore(bad.dump());});
        check(e.checkpoint()==saved,"Invalid integration state partially restored");
        rejects([&]{e.enqueueWind({NAN,1},e.snapshot().tick+1);});
        // Upgrade an L1 checkpoint; v1 did not store wind and uses the scene baseline.
        Environment old(config());auto legacy=nlohmann::json::parse(old.checkpoint());
        legacy["schema_version"]=1;legacy["scope"]="cpu-clock-and-solar";
        for(const char* field:{"initial_wind","wind","wind_target","cloud_x","cloud_z","grass_phase","wave_from","wave_to","wave_elapsed"})legacy.erase(field);
        old.restore(legacy.dump());check(old.snapshot().cloudX==96,"v1 checkpoint baseline wind upgrade failed");
        render::RenderWorldSnapshot world;world.frame.oceans.emplace_back();world.terrain.emplace();
        world.terrain->source=std::make_shared<render::TerrainPayload>();
        auto identity=pt::sceneIdentity(world);render::applyEnvironment(world,e.snapshot());
        check(world.frame.clouds.advection&&world.frame.clouds.advection->x==e.snapshot().cloudX,"Cloud bridge lost double displacement");
        check(world.terrain->vegetation->environmentWind&&world.frame.oceans[0].spectrum.windTransition,"Grass/wave bridge missing");
        check(!(pt::sceneIdentity(world)==identity),"PT identity omitted frozen wind state");
        auto frozen=world;const auto frozenIdentity=pt::sceneIdentity(frozen);const auto key=world.frame.historyKey;
        e.step();world.frame.historyKey=0;render::applyEnvironment(world,e.snapshot());
        check(world.frame.historyKey==key&&pt::sceneIdentity(frozen)==frozenIdentity&&!(pt::sceneIdentity(world)==frozenIdentity),
              "Wind revision invalidated global history or changed a detached PT snapshot");
    }
    {
        auto c=config();c.initialCloudCoverage=0;c.initialSeconds=0;
        Environment direct(c),resumed(c);direct.setEnabled(true);
        direct.enqueueCoverage(1,1);direct.enqueueWind({5,-4},1);direct.enqueueSolar({45,20},1);
        check(direct.requestedCoverage()==1&&direct.requestedSolar().elevationDegrees==45&&direct.requestedWind().x==5,
              "Mixed paused coverage intent lost");
        check(direct.snapshot().cloudCoverage==0,"Coverage event applied while paused");
        double previous=0;
        for(int i=0;i<120;++i){direct.step();double v=direct.snapshot().cloudCoverage;
            check(v>=previous&&v-previous<=1./240+1e-12,"Coverage transition discontinuous/nonmonotone");previous=v;}
        resumed.restore(direct.checkpoint());
        direct.enqueueCoverage(.15,121);resumed.enqueueCoverage(.15,121);
        direct.step();resumed.step();check(std::abs(direct.snapshot().cloudCoverage-previous)<1e-5,"Retarget teleported cloud coverage");
        direct.enqueueCoverage(0,481);resumed.enqueueCoverage(0,481);
        for(int i=0;i<720;++i){direct.step();resumed.step();}
        check(direct.checkpoint()==resumed.checkpoint()&&direct.snapshot().cloudCoverage==0,"Coverage restore diverged or never cleared exactly");
        auto saved=direct.checkpoint();auto j=nlohmann::json::parse(saved);
        for(const char* field:{"initial_cloud_coverage","cloud_coverage","coverage_from","coverage_target","coverage_elapsed"}){
            auto bad=j;bad[field]=7;rejects([&]{direct.restore(bad.dump());});check(direct.checkpoint()==saved,"Invalid coverage partially restored");}
        auto bad=j;bad["cloud_coverage"]=.1;rejects([&]{direct.restore(bad.dump());});
        rejects([&]{direct.enqueueCoverage(NAN,direct.snapshot().tick+1);});
        rejects([&]{direct.enqueueCoverage(-.1,direct.snapshot().tick+1);});
        // v2 checkpoints preserve wind, adopt the loaded scene's coverage baseline.
        auto legacy=j;legacy["schema_version"]=2;legacy["scope"]="cpu-clock-solar-wind";
        for(const char* field:{"initial_cloud_coverage","cloud_coverage","coverage_from","coverage_target","coverage_elapsed"})legacy.erase(field);
        auto upgradeConfig=c;upgradeConfig.initialCloudCoverage=.37;Environment upgrade(upgradeConfig);
        upgrade.restore(legacy.dump());check(upgrade.snapshot().cloudCoverage==.37&&upgrade.snapshot().wind.x==direct.snapshot().wind.x,"v2 coverage baseline upgrade failed");
        render::RenderWorldSnapshot world;render::applyEnvironment(world,direct.snapshot());
        check(world.frame.clouds.dynamicCoverage&&world.frame.clouds.coverage==0,"Coverage snapshot bridge missing");
        std::cout<<"Environment continuous coverage: mixed events, retarget, exact clear endpoint, checkpoint replay and v2 upgrade passed\n";
    }
    {
        auto c=config();c.initialSeconds=0;c.land={100,.002,0,0,0};c.lakeAreaSquareMeters=25;
        Environment a(c),b(c);a.setEnabled(true);b.setEnabled(true);
        a.enqueueRain(36./3600000,1);b.enqueueRain(36./3600000,1);
        check(a.requestedRain()==36./3600000&&a.snapshot().rainMetersPerSecond==0,"Rain applied before tick");
        for(int i=0;i<600;++i){a.step();if(i<300)b.step();}
        const auto w=a.snapshot();check(std::abs(w.land.storageMeters-.0001)<1e-15,"36 mm/h rain did not accumulate 0.1 mm in 10 s");
        check(std::abs(w.lakeRainCubicMeters-.0025)<1e-14,"Direct lake rainfall unit conversion/area wrong");
        Environment replay(c);replay.restore(b.checkpoint());for(int i=0;i<300;++i)replay.step();
        check(a.checkpoint()==replay.checkpoint(),"Rain budget checkpoint replay diverged");
        a.enqueueRain(1000./3600000,601);for(int i=0;i<1200;++i)a.step();
        const auto saturated=a.snapshot().land;check(saturated.storageMeters==c.land.capacityMeters&&saturated.outflowMeters>0,"Storage overflow did not exit account");
        check(std::abs(saturated.rainMeters-saturated.storageMeters-saturated.outflowMeters)<1e-14,"Rain account did not close after saturation");
        auto saved=a.checkpoint();auto j=nlohmann::json::parse(saved);
        for(const char* f:{"storage_m","rain_m","infiltration_m","evaporation_m","outflow_m"}){auto bad=j;bad["land"][f]=-1;rejects([&]{a.restore(bad.dump());});}
        auto bad=j;bad["land"]["outflow_m"]=0;rejects([&]{a.restore(bad.dump());});
        bad=j;bad["lake_rain_m3"]=100;rejects([&]{a.restore(bad.dump());});
        check(a.checkpoint()==saved,"Invalid water checkpoint partially mutated state");
        rejects([&]{a.enqueueRain(NAN,a.snapshot().tick+1);});rejects([&]{a.enqueueRain(-1,a.snapshot().tick+1);});
        c.land={10,.002,.0002,.0002,.00001};c.initialRainMetersPerSecond=0;
        Environment dry(c);dry.setEnabled(true);for(int i=0;i<100;++i)dry.step();auto d=dry.snapshot().land;
        check(d.storageMeters==0&&std::abs(d.infiltrationMeters+d.evaporationMeters-c.land.initialStorageMeters)<1e-18,"Dry sinks consumed unavailable water");
        render::RenderWorldSnapshot footprint;footprint.terrain.emplace();
        auto payload=std::make_shared<render::TerrainPayload>();payload->waterMask=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,{255,0,0,255,255,0,0,255,0,0,0,255,0,0,0,255}});
        footprint.terrain->source=payload;footprint.terrain->model[0][0]=10;footprint.terrain->model[2][2]=20;footprint.frame.oceans.emplace_back();
        auto area=render::environmentConfig(footprint,"area");footprint.frame.viewportWidth=8192;footprint.frame.viewportHeight=128;
        auto resizedArea=render::environmentConfig(footprint,"area");check(area.land.areaSquareMeters==400&&area.lakeAreaSquareMeters==400&&area.land.areaSquareMeters==resizedArea.land.areaSquareMeters,"Water budget area depends on viewport or mask partition is wrong");
        auto legacy=nlohmann::json::parse(saved);legacy["schema_version"]=3;legacy["scope"]="cpu-clock-solar-wind-coverage";
        Environment upgraded(c);upgraded.restore(legacy.dump());check(upgraded.snapshot().rainMetersPerSecond==0&&upgraded.snapshot().land.rainMeters==0,"v3 invented past rainfall");
        render::RenderWorldSnapshot world;world.terrain.emplace();world.terrain->source=std::make_shared<render::TerrainPayload>();
        render::applyEnvironment(world,w);check(world.frame.rainMetersPerSecond>0&&std::abs(world.terrain->extension.features.y-6554)<=1,"Rain/wetness bridge missing or wrong quantization");
        const auto id=pt::sceneIdentity(world);auto wet=w;wet.landWetness=1;render::applyEnvironment(world,wet);
        check(!(pt::sceneIdentity(world)==id),"Frozen PT identity did not see wet terrain");
        std::cout<<"Environment rain: metric budgets, saturation, actual sinks, lake account, replay, v3 upgrade and wetness identity passed\n";
    }
    {
        auto c=config();c.land={2,.002,0,0,0};
        auto grid=std::make_shared<SurfaceWaterGrid>();grid->elevationMeters.fill(100);
        grid->areaSquareMeters[0]=grid->areaSquareMeters[1]=1;grid->elevationMeters[0]=1;grid->elevationMeters[1]=0;c.landGrid=grid;
        Environment a(c),b(c);a.setEnabled(true);b.setEnabled(true);
        // A caller retaining a mutable configuration must not mutate the live model.
        grid->elevationMeters[0]=99;check(a.config().landGrid->elevationMeters[0]==1,"Environment retained mutable grid config");
        a.enqueueRain(36./3600000,1);b.enqueueRain(36./3600000,1);
        for(int i=0;i<600;++i){a.step();if(i<300)b.step();}
        const auto state=a.snapshot();check(state.landStorageMeters[1]>state.landStorageMeters[0]*1.3,"Downhill transfer did not create local wetness");
        check(state.landTransferredCubicMeters>0&&state.land.outflowMeters==0,"Internal transfers were counted as external flow");
        check(std::abs(state.land.rainMeters-state.land.storageMeters)<1e-15,"Spatial closed-grid budget lost water");
        Environment replay(c);replay.restore(b.checkpoint());for(int i=0;i<300;++i)replay.step();check(replay.checkpoint()==a.checkpoint(),"Spatial checkpoint replay diverged");
        auto saved=a.checkpoint();const auto json=nlohmann::json::parse(saved);
        for(int variant=0;variant<6;++variant){auto bad=json;
            if(variant==0)bad["land_storage_m"][0]=-.001;
            if(variant==1)bad["land_storage_m"][0]=.0021;
            if(variant==2)bad["land_storage_m"][2]=.001;
            if(variant==3)bad["land_storage_m"].erase(0);
            if(variant==4)bad["land_grid"]["area_m2"][0]=2;
            if(variant==5)bad["land_transferred_m3"]=-1;
            rejects([&]{a.restore(bad.dump());});check(a.checkpoint()==saved,"Spatial failed restore mutated environment");
        }
        auto old=json;old["schema_version"]=4;old["scope"]="cpu-environment-surface-water";
        Environment uniform(c);uniform.restore(old.dump());check(!uniform.snapshot().landGrid&&uniform.snapshot().land.storageMeters==state.land.storageMeters,"v4 upgrade invented spatial history");
        a.enqueueRain(1000./3600000,601);for(int i=0;i<1200;++i)a.step();
        auto saturated=a.snapshot();check(saturated.land.outflowMeters>0,"Grid capacity failed to export overflow");
        for(double depth:saturated.landStorageMeters)check(depth>=0&&depth<=c.land.capacityMeters+1e-15,"Grid donor exceeded capacity or became negative");
        check(std::abs(saturated.land.rainMeters-saturated.land.storageMeters-saturated.land.outflowMeters)<1e-13,"Grid overflow budget does not close");
        replay.restore(a.checkpoint());check(replay.checkpoint()==a.checkpoint(),"Saturated spatial checkpoint rejected");
        // A lower zero-land lake cell is an outlet; it must never accumulate land water.
        auto outlet=std::make_shared<SurfaceWaterGrid>(*a.config().landGrid);outlet->areaSquareMeters[1]=0;c.land.areaSquareMeters=1;c.landGrid=outlet;c.land.initialStorageMeters=.001;
        Environment draining(c);draining.setEnabled(true);for(int i=0;i<1200;++i)draining.step();auto d=draining.snapshot();
        check(d.land.outflowMeters>0&&d.landStorageMeters[1]==0,"Lake cell did not behave as an explicit land outlet");
        check(std::abs(.001-d.land.storageMeters-d.land.outflowMeters)<1e-15,"Outlet budget does not close");
        // Fractional catchment areas can put the weighted saturated mean a few
        // ulps above capacity; valid emitted checkpoints must always load.
        for(int variant=0;variant<8;++variant){auto fractional=std::make_shared<SurfaceWaterGrid>();auto f=config();f.land={0,.002,0,0,0};
            for(size_t i=0;i<fractional->cells;++i){fractional->areaSquareMeters[i]=(double(i%16)+.1+variant)/(2.6+double(i/16));f.land.areaSquareMeters+=fractional->areaSquareMeters[i];}
            f.landGrid=fractional;f.initialRainMetersPerSecond=1000./3600000;Environment saturated(f);saturated.setEnabled(true);
            for(int i=0;i<600;++i)saturated.step();auto checkpoint=saturated.checkpoint();Environment restored(f);restored.restore(checkpoint);
            check(checkpoint==restored.checkpoint(),"Fractional-area saturated grid cannot restore its own checkpoint");
        }
        // Prepared height page + asymmetric mask establishes height/material V orientation.
        render::RenderWorldSnapshot world;world.terrain.emplace();world.frame.oceans.emplace_back();
        auto payload=std::make_shared<render::TerrainPayload>();payload->height.heightField=true;payload->height.formats={rhi::Format::RGBA32Float};
        payload->height.rootPage={std::vector<uint8_t>(68*68*16)};
        for(int y=0;y<68;++y)for(int x=0;x<68;++x){float value=float(std::clamp(y-2,0,63))/63;std::memcpy(payload->height.rootPage[0].data()+(y*68+x)*16,&value,4);}
        auto mask=std::make_shared<render::ImageRGBA8>();mask->width=mask->height=16;mask->pixels.resize(16*16*4);
        for(int y=0;y<8;++y)for(int x=0;x<16;++x)mask->pixels[(y*16+x)*4]=255;
        payload->waterMask=mask;world.terrain->source=payload;world.terrain->model=glm::scale(glm::mat4(1),glm::vec3(8,10,8));
        auto terrainConfig=render::environmentConfig(world,"spatial-mask");check(terrainConfig.landGrid&&terrainConfig.land.areaSquareMeters==128,"Prepared height did not create a spatial catchment");
        check(terrainConfig.landGrid->areaSquareMeters[0]==1&&terrainConfig.landGrid->areaSquareMeters[15*16]==0,"Mask V orientation is reversed");
        check(std::abs(terrainConfig.landGrid->elevationMeters[0]-.3125)<1e-6,"Coarse height centre/world transform mismatch");
        Environment terrain(terrainConfig);terrain.setEnabled(true);terrain.enqueueRain(50./3600000,1);for(int i=0;i<3600;++i)terrain.step();
        const auto ground=terrain.snapshot();render::applyEnvironment(world,ground);const auto& extension=world.terrain->extension;
        check((extension.features.x&int(render::MaterialFeature::SpatialRainWetness))!=0,"Bridge omitted spatial wetness");
        check(render::rainWetness(extension,{.5f,.99f})>0&&render::rainWetness(extension,{.5f,.01f})==0,"Wet field was not flipped to material UV");
        auto id=pt::sceneIdentity(world);auto edit=ground;edit.landStorageMeters[0]=.0002;render::applyEnvironment(world,edit);check(!(id==pt::sceneIdentity(world)),"PT identity ignored spatial wetness change");
        if(const char* output=std::getenv("SCENERENDERER_SPATIAL_WATER_REPORT")){
            std::ofstream report(output);if(!report)throw std::runtime_error("Cannot open spatial water diagnostic");
            report<<nlohmann::json{{"closed_grid_10s",json},{"outlet_20s",nlohmann::json::parse(draining.checkpoint())},
                {"terrain_60s",nlohmann::json::parse(terrain.checkpoint())},{"replay_exact",true},{"invalid_restore_atomic",true},{"material_v_orientation",true}}.dump(2)<<'\n';
        }
        std::cout<<"Spatial water: conservative downhill flow, donor/capacity limits, outlets, exact replay, v4 upgrade and UV orientation passed\n";
    }
    std::cout<<"Environment wind integration, spectral targets, v1 upgrade and frozen snapshot passed\n";
    std::cout<<"Environment clock, event ordering, skipped frames, checkpoint recovery and solar bridge passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
