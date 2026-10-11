#pragma once
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
enum class PlantType : uint32_t { MeadowGrass, TallGrass, Reed, Daisy, Wildflower, Broadleaf, Conifer, Shrub };
struct VegetationSpecies {
    PlantType type=PlantType::MeadowGrass;
    uint32_t seed=1,capacity=8192;
    float density=.7f,spacing=.4f,height=.5f,width=.5f;
    float distance=100,lodDistance=25,lodTransition=8,minimumNormalY=.7f;
    // Zero farSpacing preserves uniform spacing for existing aggregate clients.
    float farSpacing=0,denseRadius=8,sparseRadius=60,farWidthScale=1;
    uint32_t variants=1; // Fixed seeded meshes; roots select a variant without duplication.
    void validate() const {
        if(!variants||variants>8||uint32_t(type)>7||!capacity||capacity>131072||seed>16777215)throw std::invalid_argument("Invalid vegetation species identity/capacity");
        for(float x:{density,spacing,height,width,distance,lodDistance,lodTransition,minimumNormalY,farSpacing,denseRadius,sparseRadius,farWidthScale})if(!std::isfinite(x))throw std::invalid_argument("Nonfinite vegetation species");
        if(farSpacing<0||(farSpacing>0&&farSpacing<spacing)||denseRadius<0||sparseRadius<=denseRadius||farWidthScale<1||farWidthScale>4)throw std::invalid_argument("Invalid vegetation distance density");
        if(density<0||density>1||spacing<.05f||height<=0||height>40||width<=0||width>30||distance<=1||distance>2000||lodDistance<=0||lodDistance>=distance||lodTransition<0||lodTransition>lodDistance||minimumNormalY<0||minimumNormalY>1)throw std::invalid_argument("Vegetation species outside domain");
    }
};
inline std::vector<VegetationSpecies> meadowSpecies(){
    return {{PlantType::MeadowGrass,11,98304,.96f,.16f,.32f,.55f,100,18,6,.65f,.58f,8,60,2.6f},
            {PlantType::TallGrass,23,16384,.6f,.45f,.8f,.65f,110,28,8,.7f,1.15f,10,70,1.8f},
            {PlantType::Reed,31,4096,.28f,1.2f,1.7f,.8f,130,40,10,.8f},
            {PlantType::Daisy,43,8192,.4f,.7f,.55f,.55f,100,22,8,.75f,1.7f,10,70,1.4f},
            {PlantType::Wildflower,59,8192,.35f,.85f,.85f,.55f,110,30,8,.7f,1.9f,10,70,1.4f},
            {PlantType::Broadleaf,71,512,.55f,9.f,8.f,8.f,200,35,10,.8f,0,8,60,1,4},
            {PlantType::Conifer,89,512,.35f,10.f,10.f,7.f,230,40,12,.75f,0,8,60,1,4},
            {PlantType::Shrub,97,1024,.4f,3.5f,1.8f,2.f,160,12,4,.7f,0,8,60,1,2}};
}
