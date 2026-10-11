// Coarse-grid CPU cost; includes common Environment wind/cloud clock work.
#include "engine/Environment.h"
#include <json/json.hpp>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
int main(){
    nlohmann::json report={{"scope","CPU fixed Environment tick; uniform vs 16x16 downhill storage; no rendering/FPS measurement"}};
    for(bool spatial:{false,true}){
        engine::EnvironmentConfig config;config.sceneKey="water-benchmark";config.initialRainMetersPerSecond=50./3600000;
        config.land.areaSquareMeters=256;config.land.initialStorageMeters=.002;
        if(spatial){auto grid=std::make_shared<engine::SurfaceWaterGrid>();grid->areaSquareMeters.fill(1);
            for(size_t i=0;i<grid->cells;++i)grid->elevationMeters[i]=std::abs(double(i%16)-7.5)+std::abs(double(i/16)-7.5);
            config.landGrid=grid;
        }
        engine::Environment environment(config);environment.setEnabled(true);
        for(int i=0;i<600;++i)environment.step();
        std::vector<double> microseconds;microseconds.reserve(36000);
        for(int i=0;i<36000;++i){auto start=std::chrono::steady_clock::now();environment.step();
            microseconds.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());}
        std::sort(microseconds.begin(),microseconds.end());const auto state=environment.snapshot();const auto& w=state.land;
        report[spatial?"spatial":"uniform"]={{"measured_ticks",microseconds.size()},{"warmup_ticks",600},
            {"p50_us",microseconds[microseconds.size()/2]},{"p95_us",microseconds[microseconds.size()*95/100]},
            {"max_us",microseconds.back()},{"land_balance_residual_m3",(config.land.initialStorageMeters+w.rainMeters-w.storageMeters-w.infiltrationMeters-w.evaporationMeters-w.outflowMeters)*config.land.areaSquareMeters},
            {"checkpoint_bytes",environment.checkpoint().size()}};
    }
    std::cout<<report.dump(2)<<'\n';
}
