#pragma once
#include <cstdint>
#include <array>
namespace pt {
// Padded 2D Sobol: each pair uses a separate dyadic index permutation and shift.
// Dimension assignments are fixed per bounce; PCG is retained for references.
uint32_t sampleHash(uint32_t value);
float sobolSample(uint32_t pixelSeed, uint32_t sample, uint32_t dimension);
struct Random {
    explicit Random(uint64_t seed = 1);
    static Random forPixel(uint64_t seed, uint32_t pixel, uint32_t sample, bool sobol);
    uint32_t bits();
    float uniform();
    std::array<float,2> uniform2();
    void dimension(uint32_t value) { if (sobol_) dimension_ = value; }
    uint64_t state;
  private:
    uint32_t pixelSeed_ = 0, sample_ = 0, dimension_ = 0;
    bool sobol_ = false;
};
} // namespace pt
