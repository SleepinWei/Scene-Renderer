#pragma once
#include "PT/CpuPathTracer.h"

namespace pt {
struct DenoiseOptions {
    // OIDN's device is independent of the rendering backend; auto may select a GPU.
    std::string device = "auto";
    bool auxiliary = true;
};
bool denoiserAvailable();
void denoise(Image &, const DenoiseOptions & = {});
// Linear Float3 PFM, with endian/scale conversion and bottom-to-top row conversion.
Image readPfm(const std::string &path);
}
