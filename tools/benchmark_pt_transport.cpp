// Single-thread trace microbenchmark; loading, acceleration build and writing are excluded.
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include "PT/ScenePackage.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
int main(int argc,char **argv){try{
    if(argc<2||argc>3)throw std::invalid_argument("Usage: pt-transport-benchmark SCENE_JSON [OUTPUT_JSON]");
    constexpr uint32_t width=160,height=90,samples=16384,depth=24;
    auto package=pt::loadScenePackage(argv[1],width,height);pt::CpuScene scene(package.snapshot);
    scene.environment=package.environment;scene.sunDirection=package.sunDirection;scene.sunIrradiance=package.sunIrradiance;scene.sunRadius=package.sunRadius;
    std::vector<glm::vec3> values;values.reserve(samples);uint64_t rays=0;double energy=0;
    auto start=std::chrono::steady_clock::now();
    for(uint32_t i=0;i<samples;++i){const auto pixel=(i*73849u)%(width*height);auto random=pt::Random::forPixel(1,pixel,i/(width*height),true);auto uv=random.uniform2();glm::vec3 origin,direction;
        scene.cameraRay((pixel%width+uv[0])/width,(pixel/width+uv[1])/height,origin,direction);
        auto value=scene.trace(origin,direction,random,depth,rays,nullptr,false);
        if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z))throw std::runtime_error("Nonfinite transport sample");
        values.push_back(value);energy+=value.x;
    }
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    nlohmann::json report={{"scene",argv[1]},{"width",width},{"height",height},{"samples",samples},{"max_depth",depth},{"seed",1},{"seconds",seconds},{"rays",rays},{"red_sum",energy},{"water_sun_proposal",false},{"threads",1},{"scope","trace microbenchmark; no film, load/build/write excluded"}};
    std::cout<<report.dump()<<'\n';
    if(argc==3){std::ofstream output(argv[2]);output<<report.dump(2)<<'\n';if(!output)throw std::runtime_error("Cannot write benchmark report");
        auto pixels=nlohmann::json::array();for(auto v:values)pixels.push_back({v.x,v.y,v.z});std::ofstream raw(std::string(argv[2])+".pixels");raw<<pixels.dump();if(!raw)throw std::runtime_error("Cannot write raw transport samples");}
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
