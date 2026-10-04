#include "engine/RenderRuntime.h"
#include "engine/QualityPolicy.h"
#include "renderer/rhi/SceneAdapter.h"
#include "rhi/ShaderAssets.h"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <array>
#include <cmath>
#include <iostream>
namespace engine {
namespace {
// Publication owns pixels independently of mutable terrain/ocean/temporal state.
// A rejected candidate may have submitted compute; never redraw its old packets.
struct PublishedImage {
    render::Resources resources;
    rhi::TextureHandle texture;
    uint32_t width, height;
    PublishedImage(std::shared_ptr<rhi::GraphicsDevice> device, uint32_t w, uint32_t h)
        : resources(std::move(device)), width(w), height(h) {
        texture = resources.texture({w, h, rhi::Format::RGBA8UNorm,
                                     rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination |
                                         rhi::TextureUsage::CopySource,
                                     "Last successful scene publication"});
    }
};
}
RenderRuntime::RenderRuntime(std::shared_ptr<rhi::GraphicsDevice> device,
                             std::unique_ptr<render::GuiRenderer> gui, size_t capacity)
    : device_(std::move(device)), queue_(capacity) {
    if (!device_ || device_->backend() == rhi::Backend::OpenGL)
        throw std::invalid_argument("Threaded renderer requires a native Metal or Vulkan device");
    device_->waitIdle(); // Quiescent ownership transfer after window/font bootstrap.
    thread_ = std::thread([this, gui = std::move(gui)]() mutable { run(std::move(gui)); });
}
RenderRuntime::~RenderRuntime() {
    try {
        finish();
    } catch (...) {
    }
}
void RenderRuntime::run(std::unique_ptr<render::GuiRenderer> gui) {
#ifdef __APPLE__
    @autoreleasepool {
#endif
        device_->adoptCurrentThread();
        std::array<double, 256> timings{};
        size_t samples = 0;
        auto publishTimings = [&] {
            const auto count = std::min(samples, timings.size());
            if (!count)
                return;
            auto sorted = timings;
            std::sort(sorted.begin(), sorted.begin() + count);
            renderP95Milliseconds_ = sorted[size_t(std::ceil(count * .95)) - 1];
            renderP99Milliseconds_ = sorted[size_t(std::ceil(count * .99)) - 1];
        };
        try {
            auto adapter=std::make_unique<render::SceneAdapter>(device_);
            QualityPolicy quality;
            std::shared_ptr<const render::TerrainPayload> originalTerrain,qualityTerrain;
            uint32_t terrainQuality=0;
            auto adaptQuality=[&](const render::RenderWorldSnapshot& original) {
                auto result=original;
                if(!quality.level())return result;
                result.frame.historyKey^=uint64_t(quality.level())*0x517cc1b727220a95ull;
                for(auto& ocean:result.frame.oceans){ocean.spectrum.size=quality.fft(ocean.spectrum.size);ocean.meshSize=quality.oceanMesh(ocean.meshSize);}
                if(result.terrain) {
                    if(originalTerrain!=result.terrain->source || terrainQuality!=quality.level()) {
                        originalTerrain=result.terrain->source;terrainQuality=quality.level();
                        auto reduced=std::make_shared<render::TerrainPayload>(*originalTerrain);
                        reduced->capacity=quality.terrainLeaves(reduced->capacity);reduced->virtualColumns=quality.virtualColumns(reduced->virtualColumns);
                        qualityTerrain=std::move(reduced);
                    }
                    result.terrain->source=qualityTerrain;
                }return result;
            };
            struct Candidate {
                std::shared_ptr<ScenePreparation> request;
                std::shared_ptr<const render::RenderWorldSnapshot> world;
                std::unique_ptr<render::SceneAdapter> adapter;
                std::unique_ptr<render::ForwardPbrRenderer> renderer;
                bool ready=false,settled=false;
                void reject(std::exception_ptr error) {if(!settled){request->completion->set_exception(error);settled=true;}}
                ~Candidate(){if(!settled)reject(std::make_exception_ptr(std::runtime_error("Scene preparation cancelled or renderer stopped")));}
            };
            std::unique_ptr<Candidate> candidate;
            std::unique_ptr<render::ForwardPbrRenderer> renderer;
            std::unique_ptr<PublishedImage> published;
            uint32_t rendererWidth = 0, rendererHeight = 0;
            auto nextAttempt = std::chrono::steady_clock::time_point::min();
            while (auto packet = queue_.pop()) {
#ifdef __APPLE__
                @autoreleasepool {
#endif
                    if(packet->prepare) {
                        candidate.reset();
                        candidate=std::make_unique<Candidate>();candidate->request=packet->prepare;candidate->world=packet->world;
                        continue;
                    }
                    if(packet->activatePrepared) {
                        if(!candidate || !candidate->ready || candidate->request->cancelled->load() ||
                           candidate->request->token!=packet->activatePrepared)
                            throw std::logic_error("Stale or unprepared scene activation");
                        adapter=std::move(candidate->adapter);renderer=std::move(candidate->renderer);
                        rendererWidth=candidate->world->frame.viewportWidth;rendererHeight=candidate->world->frame.viewportHeight;
                        candidate.reset();nextAttempt=std::chrono::steady_clock::time_point::min();
                        continue;
                    }
                    if(packet->pathTracingCapture){auto request=packet->pathTracingCapture;try{request->completion.set_value(pt::captureProcedural(request->world,device_,request->options));}catch(...){request->completion.set_exception(std::current_exception());}continue;}
                    if(packet->atmosphereCapture) {
                        auto request=packet->atmosphereCapture;
                        try {request->completion.set_value(render::bakeAtmosphere(device_,request->frame));}
                        catch(...) {request->completion.set_exception(std::current_exception());}
                        continue;
                    }
                    const auto started = std::chrono::steady_clock::now();
                    quality.setEnabled(packet->world->automaticQuality);qualityLevel_=quality.level();
                    auto snapshot=adaptQuality(*packet->world);
                    auto width = snapshot.frame.viewportWidth, height = snapshot.frame.viewportHeight;
                    const auto surface = surfaceExtent_.load();
                    device_->setPresentationExtent(surface == UINT64_MAX ? width : uint32_t(surface >> 32),
                                                   surface == UINT64_MAX ? height : uint32_t(surface));
                    device_->beginFrame();
                    if(candidate) {
                        if(candidate->request->cancelled->load())candidate.reset();
                        else if(!candidate->ready)try {
                            if(!candidate->adapter)candidate->adapter=std::make_unique<render::SceneAdapter>(device_);
                            auto frame=candidate->adapter->resolve(*candidate->world);
                            if(!frame.assetsPending) {
                                candidate->renderer=std::make_unique<render::ForwardPbrRenderer>(device_,rhi::defaultShaderDirectory(),
                                    frame.frame.viewportWidth,frame.frame.viewportHeight,render::PbrPath::Scene);
                                candidate->renderer->render(frame.frame,frame.packets,frame.exposure);
                                device_->waitIdle(); // Acknowledge actual GPU preparation before the CPU world changes.
                                candidate->ready=true;candidate->settled=true;candidate->request->completion->set_value();
                            }
                        } catch(...) {candidate->reject(std::current_exception());candidate.reset();device_->waitIdle();}
                    }
                    bool fallback = published && started < nextAttempt;
                    if (!fallback) {
                        bool retryCold=false;
                        do {
                        retryCold=false;
                        adapter->beginPublication();
                        bool rendering = false;
                        std::unique_ptr<PublishedImage> replacement;
                        try {
                            if (!published || published->width != width || published->height != height)
                                replacement = std::make_unique<PublishedImage>(device_, width, height);
                            if (!renderer) {
                                renderer = std::make_unique<render::ForwardPbrRenderer>(
                                    device_, rhi::defaultShaderDirectory(), width, height, render::PbrPath::Scene);
                                rendererWidth = width;
                                rendererHeight = height;
                            }
                            renderer->resize(width, height);
                            rendererWidth = width;
                            rendererHeight = height;
                            auto frame = adapter->resolve(snapshot);
                            meshUploadBytes_ += frame.meshUploadBytes;
                            meshUploadChunks_ += frame.meshUploadChunks;
                            pendingMeshUploads_ = frame.meshUploadsPending;
                            imageBytes_ = frame.gpuImages.residentBytes;
                            imageUploads_ = frame.gpuImages.uploads;
                            imageCacheHits_ = frame.gpuImages.hits;
                            rendering = true;
                            renderer->render(frame.frame, frame.packets, frame.exposure);
                            auto feedbackFrame=frame.frame;feedbackFrame.viewProjection=renderer->renderedViewProjection();
                            adapter->recordVirtualFeedback(feedbackFrame,renderer->depthView(),renderer->shadowVisibilityViews());
                            if (gui)
                                gui->render(packet->gui, renderer->output());
                            auto &target = replacement ? *replacement : *published;
                            auto copy = device_->createCommandList();
                            copy.copyTexture(renderer->output(), target.texture);
                            device_->submit(copy);
                            adapter->commitPublication();
                            if (replacement)
                                published = std::move(replacement);
                            nextAttempt = std::chrono::steady_clock::time_point::min();
                        } catch (const rhi::ResourceBudgetExceeded &error) {
                            adapter->rollbackPublication();
                            replacement.reset();
                            // If candidate rendering mutated effects, rebuild them on the next attempt.
                            if (rendering || (published && (rendererWidth != published->width ||
                                                           rendererHeight != published->height))) {
                                renderer.reset();
                                rendererWidth = rendererHeight = 0;
                            }
                            auto images = render::GpuImageCache::forDevice(device_);
                            images->releaseIdle();
                            device_->waitIdle(); // Complete candidate work and retirement before retry.
                            const bool reduced=quality.onPressure();
                            if(reduced) {
                                qualityLevel_=quality.level();renderer.reset();adapter->invalidateAssets();images->releaseIdle();device_->waitIdle();
                                snapshot=adaptQuality(*packet->world);
                            }
                            if(!published) {
                                if(!reduced)throw;
                                retryCold=true;continue; // At most three smaller tiers; retry this same packet.
                            }
                            ++rejectedPublications_;
                            {
                                std::lock_guard<std::mutex> lock(failureMutex_);
                                if (recoveryMessage_ != error.what())
                                    std::cerr << "Scene GPU publication rejected; keeping last image: "
                                              << error.what() << '\n';
                                recoveryMessage_ = error.what();
                            }
                            fallback = true;
                            // Avoid uploading the same oversized candidate on every UI tick.
                            nextAttempt = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                        }
                        } while(retryCold);
                    }
                    if (fallback)
                        ++fallbackFrames_;
                    width = published->width;
                    height = published->height;
                    if (!packet->screenshot.empty()) {
                        auto pixels = device_->readTexture(published->texture);
                        auto path = std::filesystem::path(packet->screenshot);
                        if (!path.parent_path().empty())
                            std::filesystem::create_directories(path.parent_path());
                        std::ofstream file(path, std::ios::binary);
                        file << "P6\n" << width << " " << height << "\n255\n";
                        for (size_t i = 0; i < pixels.size(); i += 4)
                            file.write(reinterpret_cast<const char *>(pixels.data() + i), 3);
                        if (!file)
                            throw std::runtime_error("Cannot write render screenshot");
                    }
                    device_->copyToBackbuffer(published->texture);
                    const auto sampledAt=packet->sampledAt;
                    device_->checkpoint([this,sampledAt]{
                        if(sampledAt!=std::chrono::steady_clock::time_point{}) {
                            const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-sampledAt).count();
                            peakCompletionLatency_=std::max(peakCompletionLatency_.load(),elapsed);
                        }
                    });
                    device_->present();
                    ++framesRendered_;
                    const auto memory = device_->resourceMemory();
                    peakResourceBytes_ = uint64_t(memory.peakBytes);
                    memoryPressureEvents_ = memory.pressureEvents;
                    const auto timing=device_->gpuTimingStats();if(timing.supported)peakGpuMilliseconds_=std::max(peakGpuMilliseconds_.load(),timing.peakMilliseconds);
                    const auto native=device_->nativeMemoryStats();
                    if(native.supported){nativeMemorySupported_=true;peakNativeBytes_=std::max(peakNativeBytes_.load(),native.usedBytes);}
                    const auto pipelines = device_->pipelineCacheStats();
                    pipelineBuilds_ = pipelines.graphicsBuilds + pipelines.computeBuilds;
                    pipelineCacheHits_ = pipelines.hits;
                    renderMilliseconds_ =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                            .count();
                    timings[samples % timings.size()] = renderMilliseconds_.load();
                    ++samples;
                    if (samples % 32 == 0)
                        publishTimings();
#ifdef __APPLE__
                }
#endif
            }
            device_->waitIdle();
            renderer.reset();
        } catch (...) {
            std::lock_guard<std::mutex> lock(failureMutex_);
            failure_ = std::current_exception();
            queue_.close();
        }
        // ImGui context stays on the UI thread; GPU backend destruction never touches it.
        publishTimings();
        gui.reset();
        try {
            if (device_->frameActive())
                device_->endFrame();
            device_->waitIdle();
        } catch (...) {
            std::lock_guard<std::mutex> lock(failureMutex_);
            if (!failure_)
                failure_ = std::current_exception();
        }
#ifdef __APPLE__
    }
#endif
}
bool RenderRuntime::trySubmitFrame(RenderPacket packet) {
    rethrowFailure();
    if(!packet.world || packet.prepare || packet.activatePrepared || packet.atmosphereCapture)
        throw std::invalid_argument("Nonblocking submission accepts ordinary frame snapshots only");
    if(packet.sampledAt==std::chrono::steady_clock::time_point{})packet.sampledAt=std::chrono::steady_clock::now();
    const bool accepted=queue_.tryPush(std::move(packet));if(!accepted)++skippedSnapshots_;
    rethrowFailure();return accepted;
}
bool RenderRuntime::submit(RenderPacket packet) {
    rethrowFailure();
    if (!packet.world && !packet.activatePrepared)
        throw std::invalid_argument("Render packet needs an immutable world");
    if(packet.sampledAt==std::chrono::steady_clock::time_point{})packet.sampledAt=std::chrono::steady_clock::now();
    const auto started = std::chrono::steady_clock::now();
    bool accepted = queue_.push(std::move(packet));
    const double waited =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    peakQueueWaitMilliseconds_ = std::max(peakQueueWaitMilliseconds_.load(), waited);
    rethrowFailure();
    return accepted;
}
ScenePreparationTicket RenderRuntime::prepareScene(std::shared_ptr<const render::RenderWorldSnapshot> world,
                                                        std::shared_ptr<std::atomic<bool>> cancelled) {
    if(!world)throw std::invalid_argument("Scene preparation needs a detached world");
    auto request=std::make_shared<ScenePreparation>();request->token=++nextPreparation_;
    request->cancelled=cancelled?std::move(cancelled):std::make_shared<std::atomic<bool>>(false);
    request->completion=std::make_shared<std::promise<void>>();
    ScenePreparationTicket ticket{request->token,request->completion->get_future(),request->cancelled};
    auto snapshot=std::make_shared<render::RenderWorldSnapshot>(*world);snapshot->asynchronousStreaming=true;
    RenderPacket packet;packet.world=std::move(snapshot);packet.prepare=request;
    if(!submit(std::move(packet)))throw std::runtime_error("Renderer closed during scene preparation");
    return ticket;
}
void RenderRuntime::activatePrepared(uint64_t token) {
    if(!token)throw std::invalid_argument("Empty prepared scene token");
    RenderPacket packet;packet.activatePrepared=token;
    if(!submit(std::move(packet)))throw std::runtime_error("Renderer closed during scene activation");
}
render::RenderWorldSnapshot RenderRuntime::capturePathTracingScene(const render::RenderWorldSnapshot &world,const pt::CaptureOptions &options){
    if(std::this_thread::get_id()==thread_.get_id())throw std::logic_error("PT capture cannot block its GPU owner thread");
    rethrowFailure();auto request=std::make_shared<PathTracingCapture>();request->world=world;request->options=options;auto result=request->completion.get_future();RenderPacket packet;packet.pathTracingCapture=request;
    if(!queue_.push(std::move(packet)))throw std::runtime_error("Render runtime closed during PT capture");while(result.wait_for(std::chrono::milliseconds(50))!=std::future_status::ready)rethrowFailure();rethrowFailure();return result.get();
}
render::BakedAtmosphere RenderRuntime::captureAtmosphere(const render::FrameData &frame) {
    if(std::this_thread::get_id()==thread_.get_id())throw std::logic_error("Atmosphere capture cannot block its own GPU thread");
    rethrowFailure();auto request=std::make_shared<AtmosphereCapture>();request->frame=frame;
    auto result=request->completion.get_future();RenderPacket packet;packet.atmosphereCapture=request;
    if(!queue_.push(std::move(packet)))throw std::runtime_error("Render runtime closed during atmosphere capture");
    while(result.wait_for(std::chrono::milliseconds(50))!=std::future_status::ready)rethrowFailure();
    rethrowFailure();return result.get();
}
std::string RenderRuntime::lastRecoveryMessage() const {
    std::lock_guard<std::mutex> lock(failureMutex_);
    return recoveryMessage_;
}
void RenderRuntime::rethrowFailure() const {
    std::lock_guard<std::mutex> lock(failureMutex_);
    if (failure_)
        std::rethrow_exception(failure_);
}
void RenderRuntime::finish() {
    queue_.close();
    if (thread_.joinable()) {
        thread_.join();
        device_->adoptCurrentThread();
    }
    rethrowFailure();
}
} // namespace engine
