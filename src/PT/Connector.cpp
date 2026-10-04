#include "PT/Connector.h"
#include "PT/CpuPathTracer.h"
#include "PT/ProceduralCapture.h"
#include "PT/PTRenderer.h"
#include "PT/PathTracing.h"
#include "renderer/rhi/AtmosphereBake.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "rhi/GraphicsDevice.h"
#include "system/InputManager.h"
#include "system/RenderManager.h"
#include "utils/Camera.h"
#include <algorithm>
#include <GLFW/glfw3.h>

namespace {float captureTime(){const float fixed=RenderManager::GetInstance()->setting.timeOverride;return fixed>=0?fixed:float(glfwGetTime());}}
void Connector::passDataToPTConfig(json &data) {PTConfig::GetInstance()->parse(data);}
void Connector::buildPTSceneFromRenderScene(shared_ptr<PTScene> target,const shared_ptr<RenderScene> scene) {
    auto input=InputManager::GetInstance();const auto w=uint32_t(std::max(1,input->width/2)),h=uint32_t(std::max(1,input->height/2));
    render::SceneSnapshotBuilder builder;auto snapshot=builder.capture(scene,captureTime(),w,h);
    auto frozen=pt::captureProcedural(*snapshot,rhi::graphicsDevice());target->cpuScene=std::make_shared<pt::CpuScene>(frozen);
    const auto &camera=*scene->mainCamera();
    target->addCam(std::make_shared<PTCamera>(camera.getPosition(),camera.getPosition()+camera.getFront(),camera.getUp(),camera.getZoom(),int(w),int(h)));
}
void Connector::LaunchPathTracingWithSnapshot(std::shared_ptr<const render::RenderWorldSnapshot> snapshot,const render::BakedAtmosphere &baked) {
    pt::CpuScene cpu(*snapshot);
    if(!baked.radiance.empty()) {
        cpu.environment=std::make_shared<pt::Environment>(baked.width,baked.height,baked.radiance);
        cpu.sunDirection=baked.sunDirection;cpu.sunIrradiance=baked.sunIrradiance;cpu.sunRadius=baked.sunRadius;
    }
    const auto config=PTConfig::GetInstance();pt::Options options;
    options.width=std::max(1u,snapshot->frame.viewportWidth/2);options.height=std::max(1u,snapshot->frame.viewportHeight/2);
    options.samples=uint32_t(std::max(1,config->samples));options.maxDepth=uint32_t(std::max(1,config->max_depth));options.exposure=snapshot->exposure;
    const auto prefix="build/path-tracing/editor";
    auto result=pt::render(cpu,options,[&](const pt::Image &image){pt::writeImage(image,options.exposure,prefix);pt::writeReport(image,cpu,options,prefix,"editor snapshot");});
    (void)result;
}
void Connector::LaunchPathTracingWithRenderScene(shared_ptr<RenderScene> scene) {
    const auto input=InputManager::GetInstance();std::shared_ptr<const render::RenderWorldSnapshot> snapshot;
    {render::SceneSnapshotBuilder builder;snapshot=builder.capture(scene,captureTime(),uint32_t(std::max(1,input->width)),uint32_t(std::max(1,input->height)));}
    auto controls=std::make_shared<render::RenderWorldSnapshot>(*snapshot);controls->frame.directionalEnabled=RenderManager::GetInstance()->setting.enableDirectional;snapshot=controls;
    render::BakedAtmosphere baked;
    if(snapshot->frame.sky)baked=render::bakeAtmosphere(rhi::graphicsDevice(),snapshot->frame);
    snapshot=std::make_shared<render::RenderWorldSnapshot>(pt::captureProcedural(*snapshot,rhi::graphicsDevice()));
    LaunchPathTracingWithSnapshot(std::move(snapshot),baked);
}
