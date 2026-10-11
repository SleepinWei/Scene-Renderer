#pragma once
#include "PT/CpuPathTracer.h"

namespace pt {
struct DenoiseOptions {
    // OIDN's device is independent of the rendering backend; auto may select a GPU.
    std::string device = "auto";
    bool auxiliary = true;
};
bool denoiserAvailable();
struct DenoiserStats {
    uint64_t deviceBuilds=0,filterBuilds=0,executions=0,bufferBytes=0;
};
// Construct, execute and destroy on one thread. Retains one device and the
// current extent / auxiliary configuration; never modifies raw HDR or AOVs.
class DenoiserContext {
  public:
    DenoiserContext();
    ~DenoiserContext();
    DenoiserContext(const DenoiserContext &)=delete;
    DenoiserContext &operator=(const DenoiserContext &)=delete;
    void execute(Image &,const DenoiseOptions & = {});
    const DenoiserStats &statistics() const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
void denoise(Image &, const DenoiseOptions & = {});
// Linear Float3 PFM, with endian/scale conversion and bottom-to-top row conversion.
Image readPfm(const std::string &path);
}
