#include "renderer/rhi/WeatherLighting.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <json/json.hpp>
namespace render {
void WeatherLightingSettings::validate() const {
    for(float v:{dryExtinction,rainExtinction,height,shadowInterval,cloudShadowStrength,aerialDistanceScale,aerialStartDistance,aerialMaxDistance,fogColour.x,fogColour.y,fogColour.z})if(!std::isfinite(v))throw std::invalid_argument("Weather lighting: nonfinite setting");
    if(cloudShadowStrength<0||cloudShadowStrength>1)throw std::invalid_argument("Cloud shadow strength outside [0,1]");
    if(aerialDistanceScale<0||aerialDistanceScale>10||aerialMaxDistance<100||aerialMaxDistance>200000||aerialStartDistance<0||aerialStartDistance>=aerialMaxDistance)throw std::invalid_argument("Aerial perspective: setting outside domain");
    if(dryExtinction<0||dryExtinction>.01f||rainExtinction<0||rainExtinction>.01f||height<1||height>10000||shadowInterval<0||shadowInterval>1||glm::any(glm::lessThan(fogColour,glm::vec3(0)))||glm::any(glm::greaterThan(fogColour,glm::vec3(10))))throw std::invalid_argument("Weather lighting: setting outside domain");
}
float CloudShadowField::sample(glm::vec3 p) const {
    if(projection.w==0||p.y>=projection.w||!width||transmission.size()!=size_t(width)*width)return 1;
    auto uv=(glm::vec2(p.x,p.z)-glm::vec2(projection)*(p.y-projection.z)-glm::vec2(patch))/glm::vec2(patch.z,patch.w);
    if(glm::any(glm::lessThan(uv,glm::vec2(0)))||glm::any(glm::greaterThan(uv,glm::vec2(1))))return 1;
    auto at=uv*float(width)-.5f;const int x=int(std::floor(at.x)),y=int(std::floor(at.y));auto pixel=[&](int a,int b){return transmission[size_t(std::clamp(b,0,int(width)-1))*width+std::clamp(a,0,int(width)-1)];};
    return glm::mix(glm::mix(pixel(x,y),pixel(x+1,y),at.x-x),glm::mix(pixel(x,y+1),pixel(x+1,y+1),at.x-x),at.y-y);
}
float airOpticalDepth(glm::vec3 a,glm::vec3 b,float base,float height,float extinction,float water) {
    if(a.y<=water&&b.y<=water)return 0;
    if(a.y<water)a=glm::mix(a,b,(water-a.y)/(b.y-a.y));
    else if(b.y<water)b=glm::mix(a,b,(water-a.y)/(b.y-a.y));
    // Clamp the density below the base. Split at the base to retain an analytic integral.
    auto segment=[&](glm::vec3 p,glm::vec3 q){const double distance=glm::length(q-p),h0=std::max(double(p.y-base),0.),h1=std::max(double(q.y-base),0.),d=std::abs(h1-h0)/height;
        return extinction*distance*std::exp(-std::min(h0,h1)/height)*(d<1e-5?1.-d*.5:-std::expm1(-d)/d);};
    if((a.y-base)*(b.y-base)<0){auto m=glm::mix(a,b,(base-a.y)/(b.y-a.y));return float(segment(a,m)+segment(m,b));}
    return float(segment(a,b));
}
}

namespace render {
void CloudShadowField::validate() const {
    if(!width||width>512||transmission.size()!=size_t(width)*width)throw std::invalid_argument("Cloud shadow: invalid grid extent");
    for(float v:{patch.x,patch.y,patch.z,patch.w,projection.x,projection.y,projection.z,projection.w})if(!std::isfinite(v))throw std::invalid_argument("Cloud shadow: nonfinite projection");
    if(patch.z<=0||patch.w<=0||(projection.w!=0&&projection.w<=projection.z))throw std::invalid_argument("Cloud shadow: invalid receiver domain");
    for(float value:transmission)if(!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("Cloud shadow: invalid transmission");
}
std::string serializeCloudShadow(const CloudShadowField& f){f.validate();return nlohmann::json{{"schema",1},{"scope","projected-below-cloud-solar-transmission"},{"width",f.width},{"patch",{f.patch.x,f.patch.y,f.patch.z,f.patch.w}},{"projection",{f.projection.x,f.projection.y,f.projection.z,f.projection.w}},{"transmission",f.transmission}}.dump();}
std::shared_ptr<const CloudShadowField> deserializeCloudShadow(const std::string& text){
    if(text.size()>4*1024*1024)throw std::invalid_argument("Cloud shadow: file exceeds capacity");
    auto j=nlohmann::json::parse(text);if(j.at("schema")!=1||j.at("scope")!="projected-below-cloud-solar-transmission")throw std::invalid_argument("Cloud shadow: unsupported schema");
    auto f=std::make_shared<CloudShadowField>();f->width=j.at("width").get<uint32_t>();auto p=j.at("patch").get<std::array<float,4>>(),q=j.at("projection").get<std::array<float,4>>();f->patch={p[0],p[1],p[2],p[3]};f->projection={q[0],q[1],q[2],q[3]};f->transmission=j.at("transmission").get<std::vector<float>>();f->validate();return f;
}
}
