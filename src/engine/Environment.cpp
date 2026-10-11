#include "engine/Environment.h"
#include <json/json.hpp>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <atomic>
#include <chrono>

namespace engine {
namespace {
using Json=nlohmann::json;
constexpr uint64_t maxTick=uint64_t(60)*60*60*24*365;
constexpr size_t maxCheckpointBytes=256*1024;
void validateSolar(const EnvironmentSolar& s) {
    if(!std::isfinite(s.elevationDegrees)||!std::isfinite(s.azimuthDegrees)||
       s.elevationDegrees < -90 || s.elevationDegrees > 90 ||
       s.azimuthDegrees < -360 || s.azimuthDegrees > 360)
        throw std::invalid_argument("Environment solar angles outside supported range");
}
void validateWind(EnvironmentWind w) {
    if(!std::isfinite(w.x)||!std::isfinite(w.z)||std::abs(w.x)>100||std::abs(w.z)>100)
        throw std::invalid_argument("Environment wind components must be finite and within [-100,100] m/s");
}
void validateCoverage(double value){
    if(!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("Environment cloud coverage must be finite and in [0,1]");
}
double coverageAt(double from,double target,double elapsed){const double u=elapsed/6;return from+(target-from)*u*u*(3-2*u);}
bool sameWind(EnvironmentWind a,EnvironmentWind b){return a.x==b.x&&a.z==b.z;}
Json windJson(EnvironmentWind w){return {{"x",w.x},{"z",w.z}};}
EnvironmentWind readWind(const Json& j){EnvironmentWind w{j.at("x").get<double>(),j.at("z").get<double>()};validateWind(w);return w;}
void validateRain(double value){
    if(!std::isfinite(value)||value<0||value>1000./3600000)
        throw std::invalid_argument("Environment rain must be finite and in [0,1000] mm/h");
}
void validateWater(const SurfaceWaterConfig& c,double lakeArea){
    for(double value:{c.areaSquareMeters,c.capacityMeters,c.infiltrationMetersPerSecond,c.evaporationMetersPerSecond,c.initialStorageMeters,lakeArea})
        if(!std::isfinite(value)||value<0)throw std::invalid_argument("Invalid environment surface-water configuration");
    if(c.areaSquareMeters>1e12||lakeArea>1e12||c.capacityMeters<=0||c.capacityMeters>1||c.initialStorageMeters>c.capacityMeters||
       c.infiltrationMetersPerSecond>1000./3600000||c.evaporationMetersPerSecond>1000./3600000)
        throw std::invalid_argument("Environment surface-water configuration outside supported range");
}
Json waterConfigJson(const SurfaceWaterConfig& c){return {{"area_m2",c.areaSquareMeters},{"capacity_m",c.capacityMeters},
    {"infiltration_m_per_s",c.infiltrationMetersPerSecond},{"evaporation_m_per_s",c.evaporationMetersPerSecond},{"initial_storage_m",c.initialStorageMeters}};}
SurfaceWaterConfig readWaterConfig(const Json& j){return {j.at("area_m2").get<double>(),j.at("capacity_m").get<double>(),
    j.at("infiltration_m_per_s").get<double>(),j.at("evaporation_m_per_s").get<double>(),j.at("initial_storage_m").get<double>()};}
void validateGrid(const EnvironmentConfig& c) {
    if(!c.landGrid)return;
    const auto& g=*c.landGrid;double area=0;
    if(!std::isfinite(g.cellXMeters)||!std::isfinite(g.cellZMeters)||g.cellXMeters<=0||g.cellZMeters<=0||
       !std::isfinite(g.runoffPerSecond)||g.runoffPerSecond<0||g.runoffPerSecond>1)
        throw std::invalid_argument("Invalid surface-water grid geometry/runoff");
    for(size_t i=0;i<g.cells;++i){
        if(!std::isfinite(g.areaSquareMeters[i])||g.areaSquareMeters[i]<0||g.areaSquareMeters[i]>1e12||
           !std::isfinite(g.elevationMeters[i])||std::abs(g.elevationMeters[i])>1e7)
            throw std::invalid_argument("Invalid surface-water grid cell");
        area+=g.areaSquareMeters[i];
    }
    if(c.land.areaSquareMeters<=0||std::abs(area-c.land.areaSquareMeters)>1e-10*std::max(1.,area))
        throw std::invalid_argument("Surface-water grid area differs from land account");
}
Json gridJson(const SurfaceWaterGrid& g){return {{"width",g.width},{"area_m2",g.areaSquareMeters},{"elevation_m",g.elevationMeters},
    {"cell_x_m",g.cellXMeters},{"cell_z_m",g.cellZMeters},{"runoff_per_s",g.runoffPerSecond}};}
std::shared_ptr<const SurfaceWaterGrid> readGrid(const Json& j){
    if(j.is_null())return {};
    if(j.at("width")!=SurfaceWaterGrid::width||!j.at("area_m2").is_array()||j.at("area_m2").size()!=SurfaceWaterGrid::cells||
       !j.at("elevation_m").is_array()||j.at("elevation_m").size()!=SurfaceWaterGrid::cells)
        throw std::invalid_argument("Invalid surface-water grid extent");
    auto g=std::make_shared<SurfaceWaterGrid>();g->areaSquareMeters=j.at("area_m2").get<decltype(g->areaSquareMeters)>();
    g->elevationMeters=j.at("elevation_m").get<decltype(g->elevationMeters)>();
    g->cellXMeters=j.at("cell_x_m");g->cellZMeters=j.at("cell_z_m");g->runoffPerSecond=j.at("runoff_per_s");return g;
}
void validateConfig(const EnvironmentConfig& c) {
    if(c.sceneKey.empty()||c.sceneKey.size()>4096||!std::isfinite(c.stepSeconds)||
       std::abs(c.stepSeconds-1.0/60)>1e-15||!std::isfinite(c.initialSeconds)||
       c.initialSeconds<0||c.initialSeconds>86400)
        throw std::invalid_argument("Environment requires a scene key, 60 Hz step and initial time in [0,86400]");
    validateSolar(c.initialSolar);validateWind(c.initialWind);validateCoverage(c.initialCloudCoverage);validateRain(c.initialRainMetersPerSecond);validateWater(c.land,c.lakeAreaSquareMeters);validateGrid(c);
}
uint64_t unsignedValue(const Json& value) {
    if(!value.is_number_integer()||(!value.is_number_unsigned()&&value.get<int64_t>()<0))
        throw std::invalid_argument("Environment checkpoint requires an unsigned integer");
    return value.get<uint64_t>();
}
Json solarJson(EnvironmentSolar s) {return {{"elevation_degrees",s.elevationDegrees},{"azimuth_degrees",s.azimuthDegrees}};}
EnvironmentSolar readSolar(const Json& j) {
    EnvironmentSolar s{j.at("elevation_degrees").get<double>(),j.at("azimuth_degrees").get<double>()};
    validateSolar(s);return s;
}
}
Environment::Environment(EnvironmentConfig config) {reset(std::move(config));}
void Environment::checkOwner() const {
    if(std::this_thread::get_id()!=owner_)throw std::logic_error("Environment state belongs to the logic thread");
}
void Environment::reset(EnvironmentConfig config) {
    checkOwner();validateConfig(config);
    if(config.landGrid)config.landGrid=std::make_shared<const SurfaceWaterGrid>(*config.landGrid);
    EnvironmentSnapshot fresh;fresh.epoch=state_.epoch+1;fresh.seconds=config.initialSeconds;fresh.solar=config.initialSolar;
    fresh.wind=fresh.windTarget=fresh.waveFrom=fresh.waveTo=config.initialWind;
    fresh.cloudX=config.initialWind.x*config.initialSeconds;fresh.cloudZ=config.initialWind.z*config.initialSeconds;
    fresh.grassPhase=1.7*config.initialSeconds;
    fresh.cloudCoverage=fresh.coverageFrom=fresh.coverageTarget=config.initialCloudCoverage;
    fresh.rainMetersPerSecond=config.initialRainMetersPerSecond;fresh.land.storageMeters=config.land.initialStorageMeters;
    fresh.landGrid=config.landGrid;
    if(config.landGrid)for(size_t i=0;i<SurfaceWaterGrid::cells;++i)fresh.landStorageMeters[i]=config.landGrid->areaSquareMeters[i]>0?config.land.initialStorageMeters:0;
    fresh.landWetness=std::clamp(fresh.land.storageMeters/.001,0.,1.);
    config_=std::move(config);state_=fresh;events_.clear();nextSequence_=1;
}
void Environment::setEnabled(bool enabled) {
    checkOwner();if(state_.enabled!=enabled){state_.enabled=enabled;++state_.epoch;++state_.revision;}
}
uint64_t Environment::enqueueSolar(EnvironmentSolar solar,uint64_t targetTick) {
    checkOwner();validateSolar(solar);Event e;e.tick=targetTick;e.solar=solar;return enqueue(e);
}
uint64_t Environment::enqueueWind(EnvironmentWind wind,uint64_t targetTick) {
    checkOwner();validateWind(wind);Event e;e.tick=targetTick;e.kind=Kind::Wind;e.wind=wind;return enqueue(e);
}
uint64_t Environment::enqueueCoverage(double coverage,uint64_t targetTick){
    checkOwner();validateCoverage(coverage);Event e;e.tick=targetTick;e.kind=Kind::Coverage;e.coverage=coverage;return enqueue(e);
}
uint64_t Environment::enqueueRain(double rain,uint64_t targetTick){
    checkOwner();validateRain(rain);Event e;e.tick=targetTick;e.kind=Kind::Rain;e.rain=rain;return enqueue(e);
}
double Environment::requestedRain() const {checkOwner();for(auto e=events_.rbegin();e!=events_.rend();++e)if(e->kind==Kind::Rain)return e->rain;return state_.rainMetersPerSecond;}
uint64_t Environment::enqueue(Event e) {
    const auto targetTick=e.tick;
    if(targetTick<=state_.tick||targetTick>maxTick||(!events_.empty()&&targetTick<events_.back().tick))
        throw std::invalid_argument("Environment events require ordered future ticks");
    if(events_.size()==eventCapacity)throw std::runtime_error("Environment event queue full");
    if(nextSequence_==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Environment event sequence exhausted");
    e.sequence=nextSequence_;events_.push_back(e);++nextSequence_;++state_.revision;return e.sequence;
}
void Environment::step() {
    checkOwner();if(!state_.enabled)return;
    if(state_.tick==maxTick)throw std::overflow_error("Environment one-year tick limit reached");
    ++state_.tick;
    while(!events_.empty()&&events_.front().tick<=state_.tick){
        const auto& e=events_.front();
        if(e.kind==Kind::Wind)state_.windTarget=e.wind;
        else if(e.kind==Kind::Solar)state_.solar=e.solar;
        else if(e.kind==Kind::Rain)state_.rainMetersPerSecond=e.rain;
        else {state_.coverageFrom=state_.cloudCoverage;state_.coverageTarget=e.coverage;state_.coverageElapsed=0;}
        state_.appliedSequence=e.sequence;events_.pop_front();
    }
    // Exact integral of a one-second first-order wind response. Changing the
    // target changes velocity, never rewrites the accumulated cloud position.
    const double dt=config_.stepSeconds, decay=std::exp(-dt);
    auto advance=[&](double& velocity,double target,double& position){
        const double difference=velocity-target;
        position+=target*dt+difference*(1-decay);velocity=target+difference*decay;
    };
    advance(state_.wind.x,state_.windTarget.x,state_.cloudX);
    advance(state_.wind.z,state_.windTarget.z,state_.cloudZ);
    state_.grassPhase+=dt*(1.2+.04*std::hypot(state_.wind.x,state_.wind.z));
    // Wave endpoints stay fixed during the eight-second transition. A newer
    // target starts after completion, preventing a mid-blend spectral jump.
    if(state_.waveElapsed<8)state_.waveElapsed=std::min(8.,(std::round(state_.waveElapsed/dt)+1)*dt);
    else if(!sameWind(state_.waveTo,state_.windTarget)){
        state_.waveFrom=state_.waveTo;state_.waveTo=state_.windTarget;state_.waveElapsed=0;
    }
    state_.coverageElapsed=std::min(6.,(std::round(state_.coverageElapsed/dt)+1)*dt);
    state_.cloudCoverage=state_.coverageElapsed==6?state_.coverageTarget:coverageAt(state_.coverageFrom,state_.coverageTarget,state_.coverageElapsed);
    stepWater(dt);
    state_.seconds=config_.initialSeconds+double(state_.tick)*config_.stepSeconds;++state_.revision;
}
void Environment::stepWater(double dt) {
    auto& w=state_.land;const auto& c=config_.land;const double input=state_.rainMetersPerSecond*dt;
    if(config_.landGrid){
        const auto& g=*config_.landGrid;auto& depths=state_.landStorageMeters;
        std::array<double,SurfaceWaterGrid::cells> volumes{},delta{};
        double infiltration=0,evaporation=0,outflow=0,stored=0;
        for(size_t i=0;i<g.cells;++i)if(g.areaSquareMeters[i]>0){
            depths[i]+=input;
            const double sink=std::min(depths[i],c.infiltrationMetersPerSecond*dt);depths[i]-=sink;infiltration+=sink*g.areaSquareMeters[i];
            const double vapour=std::min(depths[i],c.evaporationMetersPerSecond*dt);depths[i]-=vapour;evaporation+=vapour*g.areaSquareMeters[i];
            volumes[i]=depths[i]*g.areaSquareMeters[i];
        }
        // Simultaneous donor-limited transfers use the pre-transfer water head.
        // A lower zero-land cell is an explicit outlet (lake mask); no lake-level coupling.
        for(size_t i=0;i<g.cells;++i)if(volumes[i]>0){
            const int x=int(i%g.width),z=int(i/g.width);size_t destination=i;double slope=0;
            const double head=g.elevationMeters[i]+depths[i];
            for(int axis=0;axis<4;++axis){const int nx=x+(axis==0?-1:axis==1?1:0),nz=z+(axis==2?-1:axis==3?1:0);
                if(nx<0||nz<0||nx>=int(g.width)||nz>=int(g.width))continue;
                const size_t j=size_t(nz)*g.width+size_t(nx);const double difference=head-g.elevationMeters[j]-depths[j];
                const double candidate=difference/(axis<2?g.cellXMeters:g.cellZMeters);
                if(candidate>slope){slope=candidate;destination=j;}
            }
            if(destination==i)continue;
            const double transfer=volumes[i]*std::min(1.,g.runoffPerSecond*dt*std::clamp(slope/.1,0.,1.));
            delta[i]-=transfer;
            if(g.areaSquareMeters[destination]>0){delta[destination]+=transfer;state_.landTransferredCubicMeters+=transfer;}
            else outflow+=transfer;
        }
        for(size_t i=0;i<g.cells;++i)if(g.areaSquareMeters[i]>0){
            const double available=std::max(0.,volumes[i]+delta[i]),capacity=c.capacityMeters*g.areaSquareMeters[i];
            const double overflow=std::max(0.,available-capacity);outflow+=overflow;
            depths[i]=(available-overflow)/g.areaSquareMeters[i];stored+=available-overflow;
        }
        w.rainMeters+=input;w.infiltrationMeters+=infiltration/c.areaSquareMeters;
        w.evaporationMeters+=evaporation/c.areaSquareMeters;w.outflowMeters+=outflow/c.areaSquareMeters;w.storageMeters=stored/c.areaSquareMeters;
    }else if(c.areaSquareMeters>0){
        w.rainMeters+=input;w.storageMeters+=input;
        const double infiltration=std::min(w.storageMeters,c.infiltrationMetersPerSecond*dt);w.storageMeters-=infiltration;w.infiltrationMeters+=infiltration;
        const double evaporation=std::min(w.storageMeters,c.evaporationMetersPerSecond*dt);w.storageMeters-=evaporation;w.evaporationMeters+=evaporation;
        const double overflow=std::max(0.,w.storageMeters-c.capacityMeters);w.storageMeters-=overflow;w.outflowMeters+=overflow;
    }
    state_.landWetness=std::clamp(w.storageMeters/.001,0.,1.);state_.lakeRainCubicMeters+=input*config_.lakeAreaSquareMeters;
}
EnvironmentSnapshot Environment::snapshot() const {
    checkOwner();auto result=state_;result.pendingEvents=uint32_t(events_.size());return result;
}
const EnvironmentConfig& Environment::config() const {checkOwner();return config_;}
EnvironmentSolar Environment::requestedSolar() const {checkOwner();for(auto e=events_.rbegin();e!=events_.rend();++e)if(e->kind==Kind::Solar)return e->solar;return state_.solar;}
EnvironmentWind Environment::requestedWind() const {checkOwner();for(auto e=events_.rbegin();e!=events_.rend();++e)if(e->kind==Kind::Wind)return e->wind;return state_.windTarget;}
double Environment::requestedCoverage() const {checkOwner();for(auto e=events_.rbegin();e!=events_.rend();++e)if(e->kind==Kind::Coverage)return e->coverage;return state_.coverageTarget;}
std::string Environment::checkpoint() const {
    checkOwner();Json events=Json::array();
    for(const auto& e:events_){
        Json item{{"sequence",e.sequence},{"tick",e.tick},{"type",e.kind==Kind::Wind?"wind":e.kind==Kind::Coverage?"coverage":e.kind==Kind::Rain?"rain":"solar"}};
        if(e.kind==Kind::Wind)item["wind"]=windJson(e.wind);else if(e.kind==Kind::Coverage)item["coverage"]=e.coverage;else if(e.kind==Kind::Rain)item["rain_m_per_s"]=e.rain;else item["solar"]=solarJson(e.solar);events.push_back(std::move(item));
    }
    return Json{{"schema_version",5},{"scope","cpu-environment-spatial-water"},{"scene_key",config_.sceneKey},
        {"step_seconds",config_.stepSeconds},{"initial_seconds",config_.initialSeconds},
        {"initial_solar",solarJson(config_.initialSolar)},{"initial_wind",windJson(config_.initialWind)},
        {"wind",windJson(state_.wind)},{"wind_target",windJson(state_.windTarget)},
        {"cloud_x",state_.cloudX},{"cloud_z",state_.cloudZ},{"grass_phase",state_.grassPhase},
        {"wave_from",windJson(state_.waveFrom)},{"wave_to",windJson(state_.waveTo)},{"wave_elapsed",state_.waveElapsed},
        {"initial_cloud_coverage",config_.initialCloudCoverage},{"cloud_coverage",state_.cloudCoverage},
        {"coverage_from",state_.coverageFrom},{"coverage_target",state_.coverageTarget},{"coverage_elapsed",state_.coverageElapsed},
        {"initial_rain_m_per_s",config_.initialRainMetersPerSecond},{"rain_m_per_s",state_.rainMetersPerSecond},
        {"land_config",waterConfigJson(config_.land)},{"lake_area_m2",config_.lakeAreaSquareMeters},{"lake_rain_m3",state_.lakeRainCubicMeters},
        {"land_grid",config_.landGrid?gridJson(*config_.landGrid):Json(nullptr)},
        {"land_storage_m",state_.landStorageMeters},{"land_transferred_m3",state_.landTransferredCubicMeters},
        {"land",{{"storage_m",state_.land.storageMeters},{"rain_m",state_.land.rainMeters},{"infiltration_m",state_.land.infiltrationMeters},
                 {"evaporation_m",state_.land.evaporationMeters},{"outflow_m",state_.land.outflowMeters}}},
        {"enabled",state_.enabled},{"tick",state_.tick},
        {"solar",solarJson(state_.solar)},{"applied_sequence",state_.appliedSequence},
        {"next_sequence",nextSequence_},{"events",std::move(events)}}.dump(2);
}
void Environment::restore(const std::string& text) {
    checkOwner();if(text.size()>maxCheckpointBytes)throw std::invalid_argument("Environment checkpoint too large");
    const auto j=Json::parse(text);
    const auto version=unsignedValue(j.at("schema_version"));
    if(!((version==1&&j.at("scope")=="cpu-clock-and-solar")||(version==2&&j.at("scope")=="cpu-clock-solar-wind")||(version==3&&j.at("scope")=="cpu-clock-solar-wind-coverage")||(version==4&&j.at("scope")=="cpu-environment-surface-water")||(version==5&&j.at("scope")=="cpu-environment-spatial-water")))
        throw std::invalid_argument("Unsupported environment checkpoint schema/scope");
    EnvironmentConfig config{j.at("scene_key").get<std::string>(),j.at("step_seconds").get<double>(),
        j.at("initial_seconds").get<double>(),readSolar(j.at("initial_solar"))};
    config.initialWind=version>=2?readWind(j.at("initial_wind")):config_.initialWind;
    config.initialCloudCoverage=version>=3?j.at("initial_cloud_coverage").get<double>():config_.initialCloudCoverage;
    if(version>=4){config.initialRainMetersPerSecond=j.at("initial_rain_m_per_s").get<double>();
        config.land=readWaterConfig(j.at("land_config"));config.lakeAreaSquareMeters=j.at("lake_area_m2").get<double>();}
    else {config.land=config_.land;config.lakeAreaSquareMeters=config_.lakeAreaSquareMeters;}
    if(version>=5)config.landGrid=readGrid(j.at("land_grid"));
    // Legacy checkpoints retain their uniform-cell meaning; do not invent past flow.
    validateConfig(config);
    if(config.sceneKey!=config_.sceneKey)throw std::invalid_argument("Environment checkpoint belongs to another scene");
    EnvironmentSnapshot state;state.enabled=j.at("enabled").get<bool>();state.tick=unsignedValue(j.at("tick"));
    state.solar=readSolar(j.at("solar"));state.appliedSequence=unsignedValue(j.at("applied_sequence"));
    const auto next=unsignedValue(j.at("next_sequence"));
    if(state.tick>maxTick||next==0||next==UINT64_MAX||state.appliedSequence>=next)
        throw std::invalid_argument("Invalid environment checkpoint counters");
    const auto& entries=j.at("events");
    if(!entries.is_array()||entries.size()>eventCapacity)throw std::invalid_argument("Invalid environment event queue");
    std::deque<Event> events;auto priorTick=state.tick,priorSequence=state.appliedSequence;
    for(const auto& entry:entries){
        Event e;e.sequence=unsignedValue(entry.at("sequence"));e.tick=unsignedValue(entry.at("tick"));
        const std::string type=version==1?"solar":entry.at("type").get<std::string>();
        if(type=="wind"){e.kind=Kind::Wind;e.wind=readWind(entry.at("wind"));}
        else if(type=="solar")e.solar=readSolar(entry.at("solar"));
        else if(type=="coverage"&&version>=3){e.kind=Kind::Coverage;e.coverage=entry.at("coverage").get<double>();validateCoverage(e.coverage);}
        else if(type=="rain"&&version>=4){e.kind=Kind::Rain;e.rain=entry.at("rain_m_per_s").get<double>();validateRain(e.rain);}
        else throw std::invalid_argument("Unknown environment event type");
        if(e.tick<=state.tick||e.tick>maxTick||e.tick<priorTick||e.sequence!=priorSequence+1||e.sequence>=next)
            throw std::invalid_argument("Invalid environment checkpoint event order");
        priorTick=e.tick;priorSequence=e.sequence;events.push_back(e);
    }
    if(priorSequence!=next-1)throw std::invalid_argument("Environment checkpoint lost accepted events");
    state.seconds=config.initialSeconds+double(state.tick)*config.stepSeconds;
    if(version>=2){
        state.wind=readWind(j.at("wind"));state.windTarget=readWind(j.at("wind_target"));
        state.waveFrom=readWind(j.at("wave_from"));state.waveTo=readWind(j.at("wave_to"));
        state.cloudX=j.at("cloud_x").get<double>();state.cloudZ=j.at("cloud_z").get<double>();
        state.grassPhase=j.at("grass_phase").get<double>();state.waveElapsed=j.at("wave_elapsed").get<double>();
        for(double value:{state.cloudX,state.cloudZ,state.grassPhase,state.waveElapsed})
            if(!std::isfinite(value)||std::abs(value)>1e10)throw std::invalid_argument("Invalid environment wind integration state");
        if(state.grassPhase<0||state.waveElapsed<0||state.waveElapsed>8)throw std::invalid_argument("Invalid environment wind phase/transition");
    }else{
        // v1 never saved wind: adopt the loaded scene's baseline explicitly.
        state.wind=state.windTarget=state.waveFrom=state.waveTo=config.initialWind;
        state.cloudX=state.wind.x*state.seconds;state.cloudZ=state.wind.z*state.seconds;state.grassPhase=1.7*state.seconds;
    }
    state.cloudCoverage=state.coverageFrom=state.coverageTarget=config.initialCloudCoverage;
    if(version>=3){
        state.cloudCoverage=j.at("cloud_coverage").get<double>();state.coverageFrom=j.at("coverage_from").get<double>();
        state.coverageTarget=j.at("coverage_target").get<double>();state.coverageElapsed=j.at("coverage_elapsed").get<double>();
        validateCoverage(state.cloudCoverage);validateCoverage(state.coverageFrom);validateCoverage(state.coverageTarget);
        if(!std::isfinite(state.coverageElapsed)||state.coverageElapsed<0||state.coverageElapsed>6||
           std::abs(state.cloudCoverage-coverageAt(state.coverageFrom,state.coverageTarget,state.coverageElapsed))>1e-12)
            throw std::invalid_argument("Invalid environment coverage transition");
    }
    state.rainMetersPerSecond=config.initialRainMetersPerSecond;state.land.storageMeters=config.land.initialStorageMeters;
    if(version>=4){
        state.rainMetersPerSecond=j.at("rain_m_per_s").get<double>();validateRain(state.rainMetersPerSecond);
        const auto& w=j.at("land");state.land={w.at("storage_m").get<double>(),w.at("rain_m").get<double>(),w.at("infiltration_m").get<double>(),w.at("evaporation_m").get<double>(),w.at("outflow_m").get<double>()};
        state.lakeRainCubicMeters=j.at("lake_rain_m3").get<double>();const auto& a=state.land;
        for(double value:{a.storageMeters,a.rainMeters,a.infiltrationMeters,a.evaporationMeters,a.outflowMeters,state.lakeRainCubicMeters})
            if(!std::isfinite(value)||value<0)throw std::invalid_argument("Invalid environment water account");
        const double balance=config.land.initialStorageMeters+a.rainMeters-a.storageMeters-a.infiltrationMeters-a.evaporationMeters-a.outflowMeters;
        const double limit=double(state.tick)*config.stepSeconds*1000./3600000;
        if(a.storageMeters>config.land.capacityMeters+1e-15||a.rainMeters>limit+1e-9||state.lakeRainCubicMeters>limit*config.lakeAreaSquareMeters+1e-6||
           std::abs(balance)>1e-10*std::max(1.,a.rainMeters)||
           (config.land.areaSquareMeters==0&&(a.rainMeters!=0||a.storageMeters!=config.land.initialStorageMeters)))
            throw std::invalid_argument("Environment surface-water account does not close");
        if(config.lakeAreaSquareMeters==0&&state.lakeRainCubicMeters!=0)throw std::invalid_argument("Rain account has volume in a zero-area lake");
        if(config.land.areaSquareMeters>0&&std::abs(state.lakeRainCubicMeters-a.rainMeters*config.lakeAreaSquareMeters)>1e-9*std::max(1.,state.lakeRainCubicMeters))
            throw std::invalid_argument("Environment land/lake rainfall totals disagree");
    }
    state.landGrid=config.landGrid;
    if(version>=5){
        if(!j.at("land_storage_m").is_array()||j.at("land_storage_m").size()!=SurfaceWaterGrid::cells)
            throw std::invalid_argument("Invalid spatial water state extent");
        state.landStorageMeters=j.at("land_storage_m").get<decltype(state.landStorageMeters)>();
        state.landTransferredCubicMeters=j.at("land_transferred_m3");
        if(!std::isfinite(state.landTransferredCubicMeters)||state.landTransferredCubicMeters<0||
           state.landTransferredCubicMeters>(config.land.initialStorageMeters+state.land.rainMeters)*config.land.areaSquareMeters*double(state.tick)+1e-6)
            throw std::invalid_argument("Invalid cumulative internal water transfer");
        double stored=0;
        for(size_t i=0;i<SurfaceWaterGrid::cells;++i){const double value=state.landStorageMeters[i];
            if(!std::isfinite(value)||value<0||value>config.land.capacityMeters+1e-15||
               ((!config.landGrid||config.landGrid->areaSquareMeters[i]==0)&&value!=0))
                throw std::invalid_argument("Invalid spatial water storage");
            if(config.landGrid)stored+=value*config.landGrid->areaSquareMeters[i];
        }
        if(config.landGrid&&std::abs(stored-state.land.storageMeters*config.land.areaSquareMeters)>1e-10*std::max(1.,stored))
            throw std::invalid_argument("Spatial storage disagrees with land account");
        if(!config.landGrid&&state.landTransferredCubicMeters!=0)throw std::invalid_argument("Uniform account contains spatial transfers");
    }
    state.landWetness=std::clamp(state.land.storageMeters/.001,0.,1.);
    state.epoch=state_.epoch+1;state.revision=state_.revision+1;
    config_=std::move(config);state_=state;nextSequence_=next;events_=std::move(events);
}
void Environment::save(const std::string& path) const {
    const auto text=checkpoint();const std::filesystem::path destination(path);
    if(destination.empty())throw std::invalid_argument("Empty environment checkpoint path");
    if(destination.has_parent_path())std::filesystem::create_directories(destination.parent_path());
    static std::atomic<uint64_t> serial{0};
    const auto temp=destination.string()+".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);
    try {
        std::ofstream output(temp,std::ios::binary|std::ios::trunc);output<<text<<'\n';output.flush();
        if(!output)throw std::runtime_error("Cannot write environment checkpoint");output.close();
        if(!output)throw std::runtime_error("Cannot close environment checkpoint");
        std::filesystem::rename(temp,destination);
    }catch(...){std::error_code ignored;std::filesystem::remove(temp,ignored);throw;}
}
void Environment::load(const std::string& path) {
    checkOwner();std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot open environment checkpoint");
    const auto size=input.tellg();if(size<0||size>std::streamoff(maxCheckpointBytes))throw std::invalid_argument("Environment checkpoint too large");
    std::string text(size_t(size),'\0');input.seekg(0);input.read(text.data(),std::streamsize(text.size()));
    if(!input)throw std::runtime_error("Cannot read environment checkpoint");restore(text);
}
}
