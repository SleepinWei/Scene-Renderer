// clang++ -O3 -std=c++17 -DWATER_TRANSPORT_GENERATE -Iinclude \
//   -Iexternal tools/generate_water_transport_lut.cpp \
//   src/renderer/rhi/WaterTransport.cpp -o /tmp/generate-water-transport
// /tmp/generate-water-transport > src/renderer/rhi/water-transport-lut.inl
#include "renderer/rhi/WaterTransport.h"
#include <iostream>
#include <iomanip>
int main(){
    std::cout<<"// Generated: unit-extinction black-bottom slab, IOR 1.333 incoming Snell angle,\n"
        "// no interface Fresnel, 2048 paths/node, MT19937 seed 1337, 256-event cap.\n"
        "// 16 log(1+tau) x 16 (1-sqrt(1-albedo)) x 5 g [0,.85] x 4 air cosines [.1,1].\n"
        "// RGBA = 2+ top flux / 1 top flux / bottom flux / multi standard error.\n";
    std::cout<<std::scientific<<std::setprecision(8);
    for(const auto& v:render::waterTransportLut())std::cout<<"{"<<v.x<<"f,"<<v.y<<"f,"<<v.z<<"f,"<<v.w<<"f},\n";
}
