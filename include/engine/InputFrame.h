#pragma once
#include <array>
#include <cstdint>
namespace engine {
// One immutable value message per event-pump frame. Held buttons can be reused
// by fixed ticks; pointer deltas are consumed only once, outside catch-up ticks.
struct InputFrame {
    std::array<bool,6> movement{}; // Forward, backward, left, right, up, down.
    float movementScale=1,mouseX=0,mouseY=0,scrollY=0;
    bool mouseMoved=false,scrolled=false,viewportChanged=false;
    uint32_t width=0,height=0;
};
}
