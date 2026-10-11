#include "renderer/rhi/EnvironmentBridge.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include "renderer/rhi/MaterialWetness.h"
namespace render {
engine::EnvironmentConfig environmentConfig(const RenderWorldSnapshot& world,const std::string& sceneKey) {
    engine::EnvironmentConfig result;result.sceneKey=sceneKey;
    // Existing --time values outside the controller's startup range still work
    // while the controller is disabled; only its optional baseline is bounded.
    result.initialSeconds=std::clamp(double(world.frame.timeSeconds),0.0,86400.0);
    result.initialWind={world.frame.clouds.wind.x,world.frame.clouds.wind.y};
    result.initialCloudCoverage=world.frame.clouds.coverage;
    if(!world.frame.oceans.empty()){const auto& s=world.frame.oceans.front().spectrum;
        const auto velocity=glm::length(s.windDirection)>1e-6f?glm::normalize(s.windDirection)*s.windSpeed:glm::vec2(0);
        result.initialWind={velocity.x,velocity.y};}
    result.initialWind.x=std::clamp(result.initialWind.x,-100.,100.);
    result.initialWind.z=std::clamp(result.initialWind.z,-100.,100.);
    result.land.areaSquareMeters=0;
    if(world.terrain&&world.terrain->source){
        const auto& t=*world.terrain;
        // Horizontal projected catchment area. Lake-mask coverage is sampled
        // at source texels, independent of terrain LOD and viewport resolution.
        const glm::dvec3 x(t.model[0]),z(t.model[2]);
        const double total=4*std::abs(x.x*z.z-x.z*z.x);
        double lakeFraction=0;
        if(!world.frame.oceans.empty())if(const auto& mask=t.source->waterMask){
            if(mask->pixels.size()==size_t(mask->width)*mask->height*4&&!mask->pixels.empty()){
                size_t lakePixels=0;for(size_t i=0;i<mask->pixels.size();i+=4)lakePixels+=mask->pixels[i]>=128;
                lakeFraction=double(lakePixels)/(mask->pixels.size()/4);
            }
        }
        result.lakeAreaSquareMeters=total*lakeFraction;result.land.areaSquareMeters=total-result.lakeAreaSquareMeters;
        const auto& height=t.source->height;const auto& mask=t.source->waterMask;
        // Bootstrap from an already prepared coarse height page: no disk I/O on the logic thread.
        // Height UV increases along +Z; material/mask V runs in the opposite direction.
        if(total>0&&result.land.areaSquareMeters>0&&height.heightField&&height.rootPage.size()==1&&
           height.rootPage[0].size()==68*68*16&&height.formats.size()==1&&height.formats[0]==rhi::Format::RGBA32Float&&
           std::abs(t.model[1].x)<1e-6f&&std::abs(t.model[1].z)<1e-6f&&t.model[1].y>0&&
           (!mask||world.frame.oceans.empty()||(mask->width>=16&&mask->height>=16))){
            auto grid=std::make_shared<engine::SurfaceWaterGrid>();
            grid->cellXMeters=2*std::hypot(x.x,x.z)/grid->width;grid->cellZMeters=2*std::hypot(z.x,z.z)/grid->width;
            grid->areaSquareMeters.fill(total/grid->cells);
            if(mask&&!world.frame.oceans.empty()&&mask->pixels.size()==size_t(mask->width)*mask->height*4){
                grid->areaSquareMeters.fill(0);const double texelArea=total/(double(mask->width)*mask->height);
                for(uint32_t my=0;my<mask->height;++my)for(uint32_t mx=0;mx<mask->width;++mx)if(mask->pixels[(size_t(my)*mask->width+mx)*4]<128){
                    const size_t gx=uint64_t(mx)*grid->width/mask->width,gz=grid->width-1-uint64_t(my)*grid->width/mask->height;
                    grid->areaSquareMeters[gz*grid->width+gx]+=texelArea;
                }
            }
            auto pixel=[&](int hx,int hz){float value;std::memcpy(&value,height.rootPage[0].data()+((size_t(hz+2)*68+size_t(hx+2))*16),sizeof(value));return double(value);};
            for(size_t gz=0;gz<grid->width;++gz)for(size_t gx=0;gx<grid->width;++gx){
                const double u=(gx+.5)/grid->width,v=(gz+.5)/grid->width,px=u*63,pz=v*63;
                const int ax=int(px),az=int(pz);const double fx=px-ax,fz=pz-az;
                const double h=(pixel(ax,az)*(1-fx)+pixel(ax+1,az)*fx)*(1-fz)+(pixel(ax,az+1)*(1-fx)+pixel(ax+1,az+1)*fx)*fz;
                grid->elevationMeters[gz*grid->width+gx]=(glm::dmat4(t.model)*glm::dvec4(u*2-1,h,v*2-1,1)).y;
            }
            result.landGrid=std::move(grid);
        }
    }
    result.initialSolar={world.frame.sunAngle,world.frame.sunAzimuth};
    const auto sun=std::find_if(world.frame.lights.begin(),world.frame.lights.end(),[](const auto& l){return l.positionType.w==0;});
    if(sun!=world.frame.lights.end()){
        const auto direction=-glm::vec3(sun->directionOuter);
        if(glm::dot(direction,direction)<1e-12f)throw std::invalid_argument("Environment sun direction is zero");
        const auto d=glm::normalize(direction);
        result.initialSolar.elevationDegrees=glm::degrees(std::asin(glm::clamp(d.y,-1.f,1.f)));
        result.initialSolar.azimuthDegrees=glm::dot(glm::vec2(d.x,d.z),glm::vec2(d.x,d.z))<1e-12f?0:glm::degrees(std::atan2(d.x,-d.z));
    }
    return result;
}
void applyEnvironment(RenderWorldSnapshot& world,const engine::EnvironmentSnapshot& state) {
    if(!state.enabled){world.environment.reset();return;}
    world.environment=state;
    auto& f=world.frame;f.timeSeconds=float(state.seconds);f.weather.enabled=true;
    f.rainMetersPerSecond=float(state.rainMetersPerSecond);
    f.rainWind={float(state.wind.x),float(state.wind.z)};
    f.treeWind.velocity=f.rainWind;f.treeWind.phase=float(std::fmod(state.grassPhase,2*3.141592653589793));
    f.clouds.wind={float(state.wind.x),float(state.wind.z)};
    f.clouds.advection=glm::dvec2(state.cloudX,state.cloudZ);
    f.clouds.dynamicCoverage=true;f.clouds.coverage=float(state.cloudCoverage);
    if(world.terrain&&world.terrain->source){
        world.terrain->extension.features.x|=int(MaterialFeature::RainWetness);
        world.terrain->extension.features.y=int(std::lround(std::clamp(state.landWetness,0.,1.)*65535));
        auto& extension=world.terrain->extension;extension.features.x&=~int(MaterialFeature::SpatialRainWetness);
        if(state.landGrid){
            extension.features.x|=int(MaterialFeature::SpatialRainWetness);
            for(size_t z=0;z<16;++z)for(size_t x=0;x<16;++x)
                setRainWetCell(extension,(15-z)*16+x,uint8_t(std::lround(std::clamp(state.landStorageMeters[z*16+x]/.001,0.,1.)*255)));
        }
        auto settings=world.terrain->vegetation.value_or(world.terrain->source->vegetation);
        settings.environmentWind=true;settings.windX=float(state.wind.x);settings.windZ=float(state.wind.z);
        settings.windPhase=float(std::fmod(state.grassPhase,2*3.141592653589793));world.terrain->vegetation=settings;
    }
    const double u=std::clamp(state.waveElapsed/8.,0.,1.),blend=u*u*(3-2*u);
    for(auto& ocean:f.oceans){auto& s=ocean.spectrum;s.windTransition=state.waveElapsed<8&&(state.waveFrom.x!=state.waveTo.x||state.waveFrom.z!=state.waveTo.z);
        s.previousWind={float(state.waveFrom.x),float(state.waveFrom.z)};
        s.windDirection={float(state.waveTo.x),float(state.waveTo.z)};s.windSpeed=glm::length(s.windDirection);s.windBlend=float(blend);
    }
    f.sunAngle=float(state.solar.elevationDegrees);f.sunAzimuth=float(state.solar.azimuthDegrees);
    const auto elevation=glm::radians(f.sunAngle),azimuth=glm::radians(f.sunAzimuth);
    const glm::vec3 towardSun{std::cos(elevation)*std::sin(azimuth),std::sin(elevation),-std::cos(elevation)*std::cos(azimuth)};
    auto sun=std::find_if(f.lights.begin(),f.lights.end(),[](const auto& l){return l.positionType.w==0;});
    if(sun!=f.lights.end())sun->directionOuter=glm::vec4(-towardSun,sun->directionOuter.w);
    // Epoch invalidates restored/reenabled history. Continuous time and revision
    // do not enter this key; existing renderer-specific lighting guards remain.
    f.historyKey^=state.epoch+0x9e3779b97f4a7c15ull+(f.historyKey<<6)+(f.historyKey>>2);
}
}
