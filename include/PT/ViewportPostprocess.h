#pragma once
#include "PT/Denoiser.h"
#include <functional>
#include <future>
#include <optional>
namespace pt {
struct ViewportPostprocessJob {
    uint64_t epoch=0;
    std::shared_ptr<const void> identity;
    Image image;
    bool filter=true;
    DenoiseOptions options{"cpu",true};
    std::function<bool()> cancelled;
    std::string outputPrefix,metadata;
    float exposure=1;
    std::shared_ptr<std::promise<std::string>> exported;
};
struct ViewportPostprocessResult {
    uint64_t epoch=0;
    std::shared_ptr<const void> identity;
    Image image;
    DenoiserStats context;
    bool discarded=false;
    std::string error;
    double exportSeconds=0;
};
// One job or unreaped result at a time. No RHI calls. OIDN context is created,
// used and destroyed by the dedicated worker; shutdown drains an accepted job.
class ViewportPostprocessWorker {
  public:
    ViewportPostprocessWorker();
    ~ViewportPostprocessWorker();
    ViewportPostprocessWorker(const ViewportPostprocessWorker &)=delete;
    ViewportPostprocessWorker &operator=(const ViewportPostprocessWorker &)=delete;
    bool busy() const;
    bool submit(ViewportPostprocessJob &&); // False leaves job ownership with caller.
    std::optional<ViewportPostprocessResult> poll();
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace pt
