#pragma once
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <cstdint>
namespace render {
struct SandSample { float height; glm::vec2 gradient; glm::vec3 albedo; };
// Seeded, tileable sparse Gabor ripple field, baked once by scene creation.
// This is a bounded-kernel approximation, not the paper's analytic filter.
class ProceduralSand {
public:
    static constexpr float length=128;
    explicit ProceduralSand(uint32_t seed=173);
    SandSample sample(glm::vec2 world) const;
private:
    struct Kernel { glm::vec2 offset,frequency; float phase,weight; };
    std::array<Kernel,64*64*2> kernels_;
    uint32_t seed_;
};
}
