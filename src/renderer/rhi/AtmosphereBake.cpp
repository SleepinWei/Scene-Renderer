#include "renderer/rhi/AtmosphereBake.h"
#include "rhi/ShaderAssets.h"
#include <algorithm>
#include <cmath>
namespace render {
BakedAtmosphere bakeAtmosphere(std::shared_ptr<rhi::GraphicsDevice> device,const FrameData &frame) {
    BakedAtmosphere result;
    if(!frame.sky)return result;
    if(!device || !device->computeLimits().supported)throw std::invalid_argument("Sky bake requires a compute-capable Metal/Vulkan device");
    SunState solar;const float elevation=glm::radians(frame.sunAngle),azimuth=glm::radians(frame.sunAzimuth);
    solar.direction={std::cos(elevation)*std::sin(azimuth),std::sin(elevation),-std::cos(elevation)*std::cos(azimuth)};
    solar.irradiance=glm::vec3(frame.atmosphere.radii.x);solar.multipleScattering=frame.multipleScattering;solar.groundAlbedo=frame.groundAlbedo;
    solar.observerHeightKm=std::clamp((frame.cameraPosition.y-frame.seaLevelMeters)*.001f,.001f,frame.atmosphere.radii.z-frame.atmosphere.radii.w-.001f);
    const auto light=std::find_if(frame.lights.begin(),frame.lights.end(),[](const auto &l){return l.positionType.w==0;});
    if(light!=frame.lights.end()){solar.direction=-glm::normalize(glm::vec3(light->directionOuter));solar.irradiance=glm::vec3(light->colorInner);}
    GpuAtmosphere atmosphere(device,rhi::defaultShaderDirectory());atmosphere.update(frame.atmosphere,solar);
    const auto lut=atmosphere.read(1);constexpr int width=200,height=100;
    if(lut.size()!=size_t(width)*height*4)throw std::runtime_error("Unexpected atmosphere LUT extent");
    auto sample=[&](glm::vec3 d){const float longitude=std::atan2(d.x,-d.z),latitude=std::asin(glm::clamp(d.y,-1.f,1.f));
        float u=longitude/(2*3.14159265358979323846f);u-=std::floor(u);
        const float v=.5f+.5f*(latitude>=0?1.f:-1.f)*std::sqrt(std::abs(latitude)/(3.14159265358979323846f*.5f));
        const float x=u*width-.5f,y=v*(height-1);const int ix=int(std::floor(x)),iy=int(std::floor(y));
        auto pixel=[&](int px,int py){px=(px%width+width)%width;py=std::clamp(py,0,height-1);const size_t at=(size_t(py)*width+px)*4;return glm::vec3(lut[at],lut[at+1],lut[at+2]);};
        return glm::mix(glm::mix(pixel(ix,iy),pixel(ix+1,iy),x-ix),glm::mix(pixel(ix,iy+1),pixel(ix+1,iy+1),x-ix),y-iy);
    };
    result.radiance.reserve(size_t(result.width)*result.height);
    constexpr double pi=3.14159265358979323846;
    for(uint32_t y=0;y<result.height;++y)for(uint32_t x=0;x<result.width;++x){const double theta=(y+.5)/result.height*pi,phi=(x+.5)/result.width*2*pi;result.radiance.push_back(glm::max(sample({float(std::sin(theta)*std::sin(phi)),float(std::cos(theta)),float(-std::sin(theta)*std::cos(phi))}),glm::vec3(0)));}
    result.sunDirection=solar.direction;result.sunIrradiance=solar.irradiance*solarTransmittance(frame.atmosphere,solar);result.sunRadius=frame.atmosphere.radii.y;
    if(!frame.directionalEnabled)result.sunIrradiance=glm::vec3(0);
    return result;
}
} // namespace render
