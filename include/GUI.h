#include "engine/RenderRuntime.h"
#include "engine/Environment.h"
#pragma once
#include <imgui/imgui.h>
#include "renderer/rhi/GuiRenderer.h"
#include "rhi/ShaderAssets.h"
#include <imgui/imgui_impl_glfw.h>
#ifdef SCENERENDERER_LEGACY_METAL
#include "metal/MetalBackend.h"
#elif !defined(SCENERENDERER_METAL)
#include <imgui/imgui_impl_opengl3.h>
#endif
#include<imgui/imfilebrowser.h>
// #include<glfw/glfw3.h>
#include<glm/glm.hpp>
#include"system/Loader.h"
#include"renderer/RenderScene.h"
#include"component/Lights.h"
#include"component/Transform.h"
#include"component/Atmosphere.h"
#include "component/Cloud.h"
#include"component/TerrainComponent.h"
#include"component/Ocean.h"
#include "component/Grass.h"
#include "utils/Camera.h"
#include<imgui/imgui_toggle.h>
#include<filesystem>
#include<system/RenderManager.h>
#include<object/SkyBox.h>
#include<object/Terrain.h>
#include<component/GameObject.h>
#include<renderer/RenderPass.h>

using namespace std::filesystem;

class Gui {
public:
	std::unique_ptr<render::GuiRenderer> nativeRenderer_;
    bool nativeUi_=rhi::usesNativeRenderer();
    std::optional<SceneLoadRequest> loading_;
    bool nativeRuntimeStats_=false;float nativeRenderFps_=0;
    bool ptViewportActive_=false,ptViewportStart_=false,ptViewportPaused_=false;
    int ptViewportSamples_=256,ptViewportDepth_=8;
    pt::WorldLodSettings ptWorldLod_;
    bool ptViewportDenoise_=pt::denoiserAvailable(),ptViewportExport_=false,ptViewportExportPending_=false;
    std::string ptViewportExportMessage_;
    engine::PathTracingViewportStatus ptViewportStatus_;
    bool pauseSimulation_=false;float simulationSpeed_=1;float simulationSeconds_=0;
    engine::Environment* environment_=nullptr; // Logic-thread controller, never passed to the renderer.
    bool environmentReady_=false,environmentSingleStep_=false,environmentRainVisible_=true;
    std::string environmentSceneKey_,environmentLoadingKey_,environmentMessage_;
    char environmentCheckpointPath_[512]="build/environment/checkpoint.json";
    std::string loadError_;
    std::shared_ptr<RenderScene> stagedScene_;
    std::optional<engine::ScenePreparationTicket> preparing_;
    std::function<engine::ScenePreparationTicket(std::shared_ptr<const render::RenderWorldSnapshot>,std::shared_ptr<std::atomic<bool>>)> prepareScene_;
    std::function<void(uint64_t)> activateScene_;
	ImGui::FileBrowser fileDialog;
	const std::string base_path = "./asset/objects";
public:
	Gui(GLFWwindow* window) {
#ifdef __APPLE__
		const char* glsl_version = "#version 410 core";
#else
		const char* glsl_version = "#version 330";
#endif
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
#if defined(SCENERENDERER_METAL) || defined(SCENERENDERER_DEFAULT_VULKAN)
        extern int frameLimit;
        if(frameLimit>0)ImGui::GetIO().IniFilename=nullptr;
#endif
		ImGui::StyleColorsLight();
		if(rhi::usesNativeRenderer()){ImGui_ImplGlfw_InitForOther(window,true);nativeRenderer_=std::make_unique<render::GuiRenderer>(rhi::graphicsDevice(),rhi::defaultShaderDirectory());}else {
		#ifdef SCENERENDERER_LEGACY_METAL
		ImGui_ImplGlfw_InitForOther(window, true);
		MetalBackend::guiInitialize();
#elif !defined(SCENERENDERER_METAL)
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init(glsl_version);
#endif

		}
		fileDialog.SetTypeFilters({ ".json" });
		fileDialog.SetPwd(base_path);
	}
	void destroy() {
        if(preparing_){preparing_->cancel();preparing_.reset();stagedScene_.reset();}
        if(loading_){loading_->cancel();loading_->result.wait();loading_.reset();}
        Loader::GetInstance()->waitIdle();
        if(nativeUi_){nativeRenderer_.reset();auto& io=ImGui::GetIO();io.Fonts->SetTexID(nullptr);io.BackendRendererName=nullptr;io.BackendFlags&=~ImGuiBackendFlags_RendererHasVtxOffset;}else {
		#ifdef SCENERENDERER_LEGACY_METAL
        MetalBackend::guiShutdown();
#elif !defined(SCENERENDERER_METAL)
        ImGui_ImplOpenGL3_Shutdown();
#endif
		}
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}
	void window(std::shared_ptr<RenderScene>& scene) {
        if(loading_ && loading_->result.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            try{auto built=loading_->result.get();if(loading_->cancelled->load())throw std::runtime_error("Scene load cancelled");
                if(prepareScene_){auto payload=built->publicationPayload();if(!payload)throw std::runtime_error("Staging has no prepared CPU payload");preparing_=prepareScene_(payload,loading_->cancelled);stagedScene_=std::move(built);}
                else {scene->replaceWith(*built);environmentSceneKey_=environmentLoadingKey_;environmentReady_=false;}
                loadError_.clear();}catch(const std::exception& error){loadError_=error.what();}
            loading_.reset();
        }
        if(preparing_ && preparing_->ready.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try{preparing_->ready.get();if(preparing_->cancelled->load())throw std::runtime_error("Scene load cancelled");
                scene->replaceWith(*stagedScene_);activateScene_(preparing_->token);environmentSceneKey_=environmentLoadingKey_;environmentReady_=false;loadError_.clear();}
            catch(const std::exception& error){loadError_=error.what();preparing_->cancel();}
            preparing_.reset();stagedScene_.reset();
        }
        if(!nativeUi_){
		#ifdef SCENERENDERER_LEGACY_METAL
        MetalBackend::guiNewFrame();
#elif !defined(SCENERENDERER_METAL)
        ImGui_ImplOpenGL3_NewFrame();
#endif
		}
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

        if(environment_&&environmentReady_&&environment_->snapshot().enabled)
            ImGui::SetNextWindowSize(ImVec2(440,640),ImGuiCond_Once);
		ImGui::Begin("Info");
        ImGui::TextWrapped("Hold right mouse: look | WASD: move | Q/E: down/up");
        ImGui::Text("Framebuffer: %d x %d",InputManager::GetInstance()->width,InputManager::GetInstance()->height);
        if(environment_ && environmentReady_ && ImGui::CollapsingHeader("Environment",environment_->snapshot().enabled?ImGuiTreeNodeFlags_DefaultOpen:0)) {
            ImGui::BeginDisabled(ptViewportActive_);
            auto state=environment_->snapshot();bool enabled=state.enabled;
            if(ImGui::Checkbox("Environment clock, sun, wind, clouds and rain",&enabled))environment_->setEnabled(enabled);
            ImGui::TextWrapped("CPU environment / land-water checkpoint. Shallow-water and cloud lighting histories restart.");
            if(enabled){
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*.45f);
                ImGui::Checkbox("Pause environment",&pauseSimulation_);
                ImGui::SliderFloat("Environment speed",&simulationSpeed_,0,4);
                ImGui::BeginDisabled(!pauseSimulation_);
                if(ImGui::Button("Single environment tick"))environmentSingleStep_=true;
                ImGui::EndDisabled();
                const auto requested=environment_->requestedSolar();
                float elevation=float(requested.elevationDegrees),azimuth=float(requested.azimuthDegrees);
                bool changed=ImGui::SliderFloat("Environment sun elevation",&elevation,-18,90);
                changed=ImGui::SliderFloat("Environment sun azimuth",&azimuth,-180,180)||changed;
                if(changed)try{environment_->enqueueSolar({elevation,azimuth},state.tick+1);environmentMessage_="Solar event queued for the next CPU tick";}
                    catch(const std::exception& error){environmentMessage_=error.what();}
                const auto target=environment_->requestedWind();float wind[2]={float(target.x),float(target.z)};
                if(ImGui::SliderFloat2("Environment wind XZ (m/s)",wind,-100,100))
                    try{environment_->enqueueWind({wind[0],wind[1]},state.tick+1);environmentMessage_="Wind event queued for the next CPU tick";}
                    catch(const std::exception& error){environmentMessage_=error.what();}
                ImGui::Text("Wind %.2f / %.2f m/s | waves %.0f%%",state.wind.x,state.wind.z,100*state.waveElapsed/8);
                ImGui::Text("Cloud displacement %.2f / %.2f m",state.cloudX,state.cloudZ);
                float coverage=float(environment_->requestedCoverage());
                if(ImGui::SliderFloat("Environment cloud coverage",&coverage,0,1))
                    try{environment_->enqueueCoverage(coverage,state.tick+1);environmentMessage_="Coverage event queued for a 6 s transition";}
                    catch(const std::exception& error){environmentMessage_=error.what();}
                ImGui::Checkbox("Show rain streaks",&environmentRainVisible_);
                float rain=float(environment_->requestedRain()*3600000);
                if(ImGui::SliderFloat("Rain (mm/h)",&rain,0,100))
                    try{environment_->enqueueRain(double(rain)/3600000,state.tick+1);environmentMessage_="Rain event queued for next CPU tick";}
                    catch(const std::exception& error){environmentMessage_=error.what();}
                const auto& water=state.land;const auto& c=environment_->config();
                ImGui::Text("Land storage %.3f mm | wet %.1f%%",water.storageMeters*1000,state.landWetness*100);
                const double residual=c.land.initialStorageMeters+water.rainMeters-water.storageMeters-water.infiltrationMeters-water.evaporationMeters-water.outflowMeters;
                ImGui::Text("Land %.0f m2 | balance %.3g m3",c.land.areaSquareMeters,residual*c.land.areaSquareMeters);
                ImGui::Text("Rain %.3f | infiltration %.3f | evaporation %.3f mm",water.rainMeters*1000,water.infiltrationMeters*1000,water.evaporationMeters*1000);
                ImGui::Text("Outflow %.3f mm | direct lake rain %.3f m3",water.outflowMeters*1000,state.lakeRainCubicMeters);
                if(state.landGrid){
                    double minimum=1,maximum=0;for(size_t i=0;i<engine::SurfaceWaterGrid::cells;++i)if(state.landGrid->areaSquareMeters[i]>0){minimum=std::min(minimum,state.landStorageMeters[i]);maximum=std::max(maximum,state.landStorageMeters[i]);}
                    ImGui::Text("16 x 16 terrain cells | storage %.3f - %.3f mm",minimum*1000,maximum*1000);
                    ImGui::Text("Internal transfer %.3f m3",state.landTransferredCubicMeters);
                }
                ImGui::TextWrapped("Fixed lake level; land outflow is not added to the lake. Rain streaks illustrate rainfall.");
                ImGui::Text("Cloud coverage %.3f | transition %.0f%%",state.cloudCoverage,100*state.coverageElapsed/6);
                ImGui::TextWrapped("Wind responds in 1 s; waves blend over 8 s. A new wave target waits for the current blend.");
                ImGui::PopItemWidth();
            }
            ImGui::Text("Time %.3f s | tick %llu | CPU applied event %llu",state.seconds,
                static_cast<unsigned long long>(state.tick),static_cast<unsigned long long>(state.appliedSequence));
            auto& weather=RenderManager::GetInstance()->setting.weather;
            ImGui::Checkbox("Ground cloud shadows",&weather.cloudShadows);
            if(weather.cloudShadows)ImGui::SliderFloat("Ground cloud shadow strength",&weather.cloudShadowStrength,0,1,"%.2f");
            ImGui::Checkbox("Atmospheric sun shadows / shafts",&weather.aerialShadows);
            ImGui::Checkbox("Air extinction and ground fog",&weather.fog);
            ImGui::Checkbox("Atmospheric aerial perspective",&weather.aerialPerspective);
            if(weather.aerialPerspective){ImGui::SliderFloat("Aerial perspective distance scale",&weather.aerialDistanceScale,0,5,"%.2f");ImGui::SliderFloat("Aerial perspective start (m)",&weather.aerialStartDistance,0,5000);ImGui::SliderFloat("Aerial perspective range (m)",&weather.aerialMaxDistance,6000,96000);}
            if(weather.fog){ImGui::SliderFloat("Dry air extinction (1/m)",&weather.dryExtinction,0,.001f,"%.6f");ImGui::SliderFloat("Rain air extinction (1/m)",&weather.rainExtinction,0,.001f,"%.6f");ImGui::SliderFloat("Fog height (m)",&weather.height,1,2000);ImGui::ColorEdit3("Fog in-scattering colour",&weather.fogColour.x);}
            ImGui::SliderFloat("Cloud shadow interval (s)",&weather.shadowInterval,0,1,"%.2f");
            ImGui::Text("Pending events %u / %zu",state.pendingEvents,engine::Environment::eventCapacity);
            if(state.pendingEvents)ImGui::Text("CPU sun applied: %.2f / %.2f degrees",state.solar.elevationDegrees,state.solar.azimuthDegrees);
            ImGui::InputText("Checkpoint path",environmentCheckpointPath_,sizeof(environmentCheckpointPath_));
            if(ImGui::Button("Save environment"))try{environment_->save(environmentCheckpointPath_);environmentMessage_="CPU environment saved";}
                catch(const std::exception& error){environmentMessage_=error.what();}
            ImGui::SameLine();
            if(ImGui::Button("Restore environment"))try{environment_->load(environmentCheckpointPath_);pauseSimulation_=true;environmentSingleStep_=false;environmentMessage_="CPU environment restored and paused; GPU histories will restart";}
                catch(const std::exception& error){environmentMessage_=error.what();}
            if(!environmentMessage_.empty())ImGui::TextWrapped("%s",environmentMessage_.c_str());
            ImGui::EndDisabled();
        }
        if(nativeRuntimeStats_ && ImGui::CollapsingHeader("Path tracing viewport")) {
            ImGui::TextWrapped("R: capture / restart GPU PT. Simulation time is frozen. Moving uses Raster; PT restarts after the camera settles.");
            ImGui::SliderInt("PT sample target",&ptViewportSamples_,1,4096);
            ImGui::SliderInt("PT depth",&ptViewportDepth_,1,32);
            ImGui::Checkbox("PT clustered world LOD",&ptWorldLod_.enabled);
            if(ptWorldLod_.enabled){ImGui::SliderFloat("PT LOD displacement (m)",&ptWorldLod_.maxError,.005f,.5f,"%.3f");ImGui::SliderFloat("PT LOD target triangle ratio",&ptWorldLod_.targetRatio,.1f,1.f);ImGui::SliderFloat("PT LOD UV error",&ptWorldLod_.maxUvError,0,.1f,"%.3f");ImGui::TextWrapped("Applied at capture. Boundaries, cutouts, emitters and optical interfaces keep full detail.");}
            if(ImGui::Button(ptViewportActive_?"Restart frozen PT":"Capture GPU PT"))ptViewportStart_=true;
            if(ptViewportActive_) {
                ImGui::Checkbox("Pause PT sampling",&ptViewportPaused_);
                if(ImGui::Button("Cancel PT / Raster"))ptViewportActive_=false;
            }
            if(pt::denoiserAvailable())ImGui::Checkbox("OIDN preview",&ptViewportDenoise_);
            else ImGui::TextWrapped("OIDN is unavailable in this build; the viewport shows raw samples.");
            if(ptViewportActive_&&ptViewportStatus_.samples>0&&!ptViewportExportPending_&&ImGui::Button("Export linear checkpoint"))ptViewportExport_=true;
            if(ptViewportExportPending_)ImGui::Text("Export pending...");
            if(!ptViewportExportMessage_.empty())ImGui::TextWrapped("%s",ptViewportExportMessage_.c_str());
            if(ptViewportDenoise_)ImGui::Text("OIDN: %u spp | %s | %.1f ms",ptViewportStatus_.denoisedSamples,ptViewportStatus_.denoiserBusy?"filtering":"ready",ptViewportStatus_.denoiseSeconds*1000);
            if(!ptViewportStatus_.denoiseMessage.empty())ImGui::TextWrapped("%s",ptViewportStatus_.denoiseMessage.c_str());
            ImGui::Text("PT: %s | %u / %u spp",ptViewportStatus_.state.c_str(),ptViewportStatus_.samples,ptViewportStatus_.targetSamples);
            if(ptViewportStatus_.previewFrozen)ImGui::TextUnformatted("Preparing new PT image; showing the previous preview");
            ImGui::Text("Scene uploads: %llu | reuse: %llu | %.2f MiB",static_cast<unsigned long long>(ptViewportStatus_.sceneUploads),static_cast<unsigned long long>(ptViewportStatus_.sceneCacheHits),ptViewportStatus_.sceneUploadBytes/1048576.0);
            if(ptViewportStatus_.state=="Uploading")ImGui::Text("Preparation: %.2f / %.2f MiB | %llu steps",ptViewportStatus_.preparationBytes/1048576.0,ptViewportStatus_.preparationTotalBytes/1048576.0,static_cast<unsigned long long>(ptViewportStatus_.preparationSteps));
            ImGui::Text("PT pipeline / allocation / upload: %.1f / %.1f / %.1f ms",ptViewportStatus_.pipelineSeconds*1000,ptViewportStatus_.allocationSeconds*1000,ptViewportStatus_.uploadSeconds*1000);
            ImGui::Text("TLAS build / refit / reuse: %llu / %llu / %llu",static_cast<unsigned long long>(ptViewportStatus_.tlasBuilds),static_cast<unsigned long long>(ptViewportStatus_.tlasRefits),static_cast<unsigned long long>(ptViewportStatus_.tlasReuses));
            ImGui::Text("Retained GPU / BLAS: %.1f / %.1f MiB",ptViewportStatus_.gpuCacheBytes/1048576.0,ptViewportStatus_.blasCacheBytes/1048576.0);
            if(!ptViewportStatus_.message.empty())ImGui::TextWrapped("%s",ptViewportStatus_.message.c_str());
        }

		if (ImGui::CollapsingHeader("Scene loading")) {
            if(preparing_)ImGui::Text("Preparing GPU scene...");
            if(loading_)ImGui::Text("Loading %zu / %zu",loading_->completed->load(),loading_->total->load());
            if((loading_ || preparing_) && ImGui::Button("Cancel load")) {
                if(loading_)loading_->cancel();
                if(preparing_){preparing_->cancel();preparing_.reset();stagedScene_.reset();}
            }
            ImGui::Checkbox("Pause simulation",&pauseSimulation_);
        ImGui::SliderFloat("Simulation speed",&simulationSpeed_,0,4);
        if(!loadError_.empty())ImGui::TextWrapped("%s",loadError_.c_str());
			if (ImGui::Button("Select Scene")) {
				fileDialog.Open();
			}
		}
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Camera")) {
			if (scene->mainCamera()) {
                ImGui::TextWrapped("Shift: faster | Alt: slower | Scroll: field of view");
                float pitch=scene->mainCamera()->getPitch();
                if(ImGui::SliderFloat("View pitch",&pitch,-89,89))scene->mainCamera()->setAngles(scene->mainCamera()->getYaw(),pitch);
                bool fixed=scene->mainCamera()->isFixed();
                if(ImGui::Checkbox("Lock camera",&fixed))scene->mainCamera()->setFixed(fixed);
				// exposure
				float exposure=scene->mainCamera()->getExposure();
                if(ImGui::SliderFloat("exposure", &exposure, 0.5f, 6.0f))scene->mainCamera()->setExposure(exposure);
			}
		}

		if (ImGui::CollapsingHeader("Rendering")) {
			auto &setting = RenderManager::GetInstance()->setting;
			static bool enableShadow = setting.enableShadow;
			if (ImGui::Toggle("Enable Shadow", &enableShadow)) {
				setting.enableShadow = enableShadow;
			}
			if(nativeUi_ && setting.enableShadow){
                auto& shadows=setting.shadowSettings;
                ImGui::Checkbox("PCSS soft shadows",&shadows.pcss);
                ImGui::Checkbox("Shadow GPU instance culling",&shadows.instanceCulling);
                ImGui::SliderFloat("Shadow distance",&shadows.distance,10,5000);
                ImGui::SliderFloat("Cascade blend",&shadows.cascadeBlend,0,.3f);
                ImGui::SliderFloat("Shadow bias (world units)",&shadows.depthBias,0,.05f,"%.4f");
                ImGui::SliderFloat("Local light radius",&shadows.localLightRadius,0,1);
            }
            if(nativeUi_ && ImGui::CollapsingHeader("DDGI diffuse global illumination")){
                auto& gi=setting.ddgi;
                ImGui::Checkbox("Enable DDGI",&gi.enabled);
                ImGui::TextDisabled("Stage 1: local probes / raster capture");
                ImGui::DragFloat3("Volume origin (m)",&gi.origin.x,.1f);
                ImGui::SliderInt3("Probe grid",&gi.counts.x,2,18);
                gi.debugLayer=std::min(gi.debugLayer,gi.counts.y-1);
                ImGui::SliderFloat("Probe spacing (m)",&gi.spacing,.25f,50);
                ImGui::SliderInt("Probes per frame",&gi.probesPerFrame,1,64);
                ImGui::SliderInt("Capture face resolution",&gi.captureResolution,4,32);
                ImGui::SliderFloat("Probe trace distance",&gi.maxDistance,gi.spacing,1000);
                ImGui::SliderFloat("GI intensity",&gi.intensity,0,4);
                ImGui::Checkbox("Probe visibility / leak control",&gi.visibility);
                ImGui::SliderFloat("Visibility power",&gi.visibilityPower,1,8);
                ImGui::SliderFloat("Backface rejection threshold",&gi.backfaceThreshold,.1f,1);
                ImGui::SliderFloat("GI normal bias",&gi.normalBias,0,gi.spacing*.5f);
                ImGui::SliderFloat("GI view bias",&gi.viewBias,0,gi.spacing*.5f);
                ImGui::Checkbox("Multi bounce feedback",&gi.multiBounce);
                ImGui::SliderFloat("Bounce gain",&gi.bounceGain,0,.95f);
                ImGui::SliderFloat("Probe hysteresis",&gi.hysteresis,0,.98f);
                ImGui::Combo("Show probes",&gi.debugProbes,"Off\0Directional irradiance\0Probe classification\0");
                if(gi.debugProbes){
                    ImGui::Checkbox("Probe X-ray",&gi.debugXray);
                    ImGui::SliderInt("Probe Y layer (-1 = all)",&gi.debugLayer,-1,gi.counts.y-1);
                    ImGui::SliderFloat("Probe marker radius (m)",&gi.debugRadius,.02f,1.f);
                    ImGui::TextDisabled("Green: active / red: rejected / yellow: unvisited");
                }
                if(ImGui::Button("Reset DDGI history"))++gi.resetSerial;
                gi.normalBias=std::min(gi.normalBias,gi.spacing*.5f);gi.viewBias=std::min(gi.viewBias,gi.spacing*.5f);gi.maxDistance=std::max(gi.maxDistance,gi.spacing);
                const auto count=gi.probeCount();
                ImGui::Text("%u probes / %.2f MiB cache",count,double(gi.cacheBytes())/(1024*1024));
                ImGui::Text("Full sweep: %u frames (+ temporal convergence)",(count+gi.probesPerFrame-1)/gi.probesPerFrame);
            }
            ImGui::Toggle("RSM lighting demo", &setting.enableRSM);
            if(setting.enableRSM)ImGui::TextDisabled("Demo only; no further lighting development planned.");
            if(setting.enableRSM && nativeUi_){
                auto& rsm=setting.rsmSettings;
                ImGui::Checkbox("Sun and sky RSM",&rsm.useSunSky);ImGui::Checkbox("Sun bounce",&rsm.sunBounce);ImGui::Checkbox("Sky bounce",&rsm.skyBounce);
                ImGui::SliderFloat("RSM world radius",&rsm.worldRadius,2,80);ImGui::SliderFloat("RSM intensity",&rsm.intensity,0,4);
                ImGui::SliderFloat("RSM UV radius",&rsm.sampleRadius,.01f,1);ImGui::SliderInt("RSM samples",&rsm.sampleCount,1,256);ImGui::Checkbox("RSM indirect only",&rsm.indirectOnly);
            }else if (setting.enableRSM) {
                auto rsm = RenderManager::GetInstance()->rsmPass;
                ImGui::Checkbox("Sun and sky RSM", &rsm->useSunSky);
                ImGui::Checkbox("Sun bounce", &rsm->sunBounce);
                ImGui::Checkbox("Sky bounce", &rsm->skyBounce);
                ImGui::SliderFloat("RSM world radius", &rsm->worldRadius, 2.0f, 80.0f);
                ImGui::SliderFloat("RSM intensity", &rsm->intensity, 0.0f, 4.0f);
                ImGui::SliderFloat("RSM UV radius", &rsm->sampleRadius, 0.01f, 1.0f);
                ImGui::SliderInt("RSM samples", &rsm->sampleCount, 1, 256);
                ImGui::Checkbox("RSM indirect only", &rsm->indirectOnly);
            }
			static bool enableDirectional= setting.enableDirectional;
			if (ImGui::Toggle("Enable Directional", &enableDirectional)) {
				setting.enableDirectional= enableDirectional;
			}

			ImGui::Toggle(nativeUi_?"Enable ambient occlusion":"Enable SSAO", &setting.enableSSAO);
            ImGui::Toggle("Enable TSAA", &setting.enableTSAA);
            if(nativeUi_&&setting.enableTSAA)ImGui::Checkbox("TAA high quality",&setting.taaHighQuality);
            if(nativeUi_)ImGui::Checkbox("Automatic quality under memory pressure",&setting.automaticQuality);
            if(nativeUi_){ImGui::Checkbox("Hierarchical cluster geometry",&setting.clusters.enabled);
                if(setting.clusters.enabled){ImGui::SliderFloat("Cluster LOD error (pixels)",&setting.clusters.pixelError,0.f,8.f,"%.2f");ImGui::Checkbox("Cluster triangle wireframe",&setting.clusters.wireframe);ImGui::Checkbox("Cluster Hi-Z occlusion",&setting.clusters.occlusion);if(setting.clusters.occlusion){ImGui::Checkbox("Two-stage cluster occlusion",&setting.clusters.twoPhase);
                    if(setting.clusters.twoPhase&&ImGui::TreeNode("Cluster occlusion A/B")){ImGui::Checkbox("Coarse fused Hi-Z pyramid",&setting.clusters.fusedPyramid);ImGui::Checkbox("Fuse late prefix and append",&setting.clusters.fusedLate);ImGui::Checkbox("Conservative XY intervals (experimental)",&setting.clusters.conservativeIntervals);ImGui::TreePop();}}}}
            if(nativeUi_){ImGui::Checkbox("Deferred shading",&setting.useDefer);ImGui::Checkbox("HDR tone mapping",&setting.enableHDR);}
			if(nativeUi_){
                ImGui::Checkbox("GTAO horizon integration",&setting.aoHorizon);
                ImGui::Checkbox("AO edge-aware denoise",&setting.aoDenoise);
                ImGui::SliderFloat("AO radius (world units)",&setting.aoRadius,0.f,5.f);
                ImGui::SliderFloat("AO bias (world units)",&setting.aoBias,0.f,.2f);
                ImGui::SliderFloat("AO contrast",&setting.aoPower,.1f,4.f);
                if(setting.aoHorizon){ImGui::SliderInt("AO slices",&setting.aoSlices,2,8);ImGui::SliderInt("AO steps per side",&setting.aoSteps,2,8);}
            }else ImGui::SliderFloat("SSAO radius", &(RenderManager::GetInstance()->ssaoPass->radius),0.0f,0.5f);
		}

		ImGui::Separator();
        if(nativeUi_ && ImGui::CollapsingHeader("Post processing")) {
            auto& p=RenderManager::GetInstance()->setting.postProcess;
            ImGui::Checkbox("Enable post effects",&p.enabled);
            if(ImGui::Button("Reset post effects"))p=render::PostProcessSettings{};
            ImGui::SameLine();
            if(ImGui::Button("Soft cinematic preset")) {
                p=render::PostProcessSettings{};p.toneMapper=render::ToneMapper::ACES;
                p.bloom=true;p.bloomStrength=.12f;p.colorGrading=true;p.saturation=.95f;
                p.vignette=true;p.vignetteStrength=.2f;
            }
            if(p.enabled) {
                int mapper=int(p.toneMapper);
                if(ImGui::Combo("Tone mapper",&mapper,"Exponential\0ACES fitted\0Reinhard\0Linear clamp\0"))p.toneMapper=render::ToneMapper(mapper);
                ImGui::SliderFloat("Post exposure (EV)",&p.exposureEV,-5,5);
                ImGui::Checkbox("Bloom",&p.bloom);
                if(p.bloom){
                    ImGui::SliderFloat("Bloom threshold (HDR)",&p.bloomThreshold,0,10);
                    ImGui::SliderFloat("Bloom soft knee",&p.bloomKnee,0,2);
                    ImGui::SliderFloat("Bloom strength",&p.bloomStrength,0,1);
                    ImGui::SliderFloat("Bloom scatter",&p.bloomScatter,0,1);
                }
                ImGui::Checkbox("Depth of field",&p.depthOfField);
                if(p.depthOfField){
                    ImGui::SliderFloat("Focus distance (m)",&p.focusDistance,.1f,200);
                    ImGui::SliderFloat("Focus transition (m)",&p.focusRange,.1f,50);
                    ImGui::SliderFloat("DOF radius (pixels)",&p.dofRadius,0,32);
                    ImGui::TextWrapped("Approximate depth gather; transparent water and glass use the opaque depth behind them.");
                }
                ImGui::Checkbox("Camera motion blur",&p.motionBlur);
                if(p.motionBlur){ImGui::SliderFloat("Shutter fraction",&p.shutter,0,2);ImGui::SliderFloat("Maximum motion (pixels)",&p.motionMaxPixels,0,64);}
                ImGui::Checkbox("Color grading",&p.colorGrading);
                if(p.colorGrading){ImGui::SliderFloat("Saturation",&p.saturation,0,2);ImGui::SliderFloat("Contrast",&p.contrast,0,2);ImGui::SliderFloat("Warm / cool",&p.temperature,-1,1);ImGui::SliderFloat("Green / magenta",&p.tint,-1,1);}
                ImGui::Checkbox("FXAA",&p.fxaa);
                ImGui::Checkbox("Sharpen",&p.sharpen);
                if(p.sharpen)ImGui::SliderFloat("Sharpness",&p.sharpness,0,1);
                ImGui::Checkbox("Vignette",&p.vignette);
                if(p.vignette){ImGui::SliderFloat("Vignette strength",&p.vignetteStrength,0,1);ImGui::SliderFloat("Vignette roundness",&p.vignetteRoundness,0,1);}
                ImGui::Checkbox("Chromatic aberration",&p.chromaticAberration);
                if(p.chromaticAberration)ImGui::SliderFloat("Chromatic offset (pixels)",&p.chromaticPixels,0,8);
                ImGui::Checkbox("Film grain",&p.filmGrain);
                if(p.filmGrain)ImGui::SliderFloat("Grain strength",&p.grainStrength,0,.2f);
            }
        }
        ImGui::Separator();
		if (ImGui::CollapsingHeader("Point Light")) {
			auto& lights = scene->pointLights();
			for (int i = 0; i < lights.size(); i++) {
				char title[] = "Lighti Position";
				title[5] = '0' + i;
				ImGui::Text(title);
				//auto& light = lights[0]; 
				auto&& lightTrans = std::static_pointer_cast<Transform>(
					lights[i]->owner()->GetComponent("Transform"));
				auto position=lightTrans->getPosition();
                if (ImGui::SliderFloat3("Position", &position.x, -10.0f, 10.0f)) {
                    lightTrans->setPosition(position);lights[i]->setDirtyFlag(true);
                }
				//ImGui::Text("Light Position: (%f,%f,%f)", lightTrans->getPosition().x, lightTrans->getPosition().y, lightTrans->getPosition().z);
			}
		}
		if(ImGui::CollapsingHeader("Direction Light")){
			auto& dlights = scene->directionLights();
            const bool environmentSun=environment_&&environmentReady_&&environment_->snapshot().enabled;
            const auto primary=std::find_if(dlights.begin(),dlights.end(),[](const auto& light){return light&&light->isEnabled();});
			for (int i = 0; i < dlights.size(); i++) {
				char title[] = "Direction Lighti";
				title[15] = '0' + 0;
				ImGui::Text(title);
				//auto& light = lights[0]; 
				auto&& lightTrans = std::static_pointer_cast<Transform>(
					dlights[i]->owner()->GetComponent("Transform"));
				auto lightData = dlights[i]->getData();
				//bool change1 = ImGui::SliderFloat3("Position", (float*)&lightTrans->getPosition(), -10.0f, 1.0f);
				bool change1 = false; 
                bool change2=false;
                if(environmentSun&&primary!=dlights.end()&&dlights[i]==*primary)
                    ImGui::TextWrapped("Sun direction is controlled by Environment. Disable its clock to edit manually.");
                else change2=ImGui::SliderFloat3("Direction", (float*)&lightData.direction, -1.0f, 1.0f);
				// ImGui::Text("Light Position: (%f,%f,%f)", lightTrans->getPosition().x, lightTrans->getPosition().y, lightTrans->getPosition().z);
				// ImGui::Text("Light Direction: (%f,%f,%f)", lightData.direction.x, lightData.direction.y, lightData.direction.z);
			
				if (change1 || change2) {
					if(glm::dot(lightData.direction,lightData.direction)>1e-10f)dlights[i]->setData(lightData);
				}
			}
		}
		if (ImGui::CollapsingHeader("Spot Light")) {
			auto& slights = scene->spotLights();
			for (int i = 0; i < slights.size(); i++) {
				char title[] = "Spot Lighti Position";
				title[15] = '0' + 0;
				ImGui::Text(title);
				//auto& light = lights[0]; 
				auto&& lightTrans = std::static_pointer_cast<Transform>(
					slights[i]->owner()->GetComponent("Transform"));
				auto&& lightData = slights[i]->getData();
				//bool change1 = ImGui::SliderFloat3("Position", (float*)&lightTrans->getPosition(), -10.0f, 1.0f);
				//bool change2 = ImGui::SliderFloat3("Direction", (float*)&lightData.direction, -1.0f, 1.0f);
				ImGui::Text("Light Position: (%f,%f,%f)", lightTrans->getPosition().x, lightTrans->getPosition().y, lightTrans->getPosition().z);
				ImGui::Text("Light Direction: (%f,%f,%f)", lightData.direction.x, lightData.direction.y, lightData.direction.z);
			
				ImGui::Text("CutOff: %f", lightData.cutOff);
				ImGui::Text("OuterCutOff: %f", lightData.outerCutOff);
				//if (change1 || change2) {
					//slights[i]->setDirtyFlag(true);
				//}
			}
		}

		// sky 
		if (scene->sky()) {
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Atmosphere")) {
				auto&& atmos = std::static_pointer_cast<Atmosphere>(scene->sky()->GetComponent("Atmosphere"));
				auto atmosphereSettings=atmos->settings();
                auto& atmosParam = atmosphereSettings.atmosphere;
				auto& sunAngle = atmosphereSettings.sunAngle;
                if(environment_&&environmentReady_&&environment_->snapshot().enabled){
                    const auto solar=environment_->snapshot().solar;
                    ImGui::TextWrapped("Environment sun: elevation %.2f, azimuth %.2f degrees",solar.elevationDegrees,solar.azimuthDegrees);
                }else {
                    ImGui::SliderFloat("Sun elevation", &sunAngle, -20.0f, 90.0f);
                    ImGui::SliderFloat("Sun azimuth", &atmosphereSettings.sunAzimuth, -180.0f, 180.0f);
                }
                float radiusDegrees=glm::degrees(atmosParam.sun_angular_radius);
                if(ImGui::SliderFloat("Sun angular radius (degrees)",&radiusDegrees,.05f,2.f))atmosParam.sun_angular_radius=glm::radians(radiusDegrees);
                ImGui::SliderFloat("Multiple scattering", &atmosphereSettings.multipleScattering, 0, 2);
                ImGui::SliderFloat("Ground albedo", &atmosphereSettings.groundAlbedo, 0, 1);
                ImGui::InputFloat("Sea level (m)", &atmosphereSettings.seaLevelMeters);
				ImGui::SliderFloat("mie_g", &atmosParam.mie_g, 0.0f, .99f);
				ImGui::SliderFloat3("rayleigh_scattering", (float*)&atmosParam.rayleigh_scattering, 0.0f, 1.0f);
                try{atmos->setSettings(atmosphereSettings);}catch(const std::exception& error){loadError_=error.what();}
			}

            if(auto cloud=scene->sky()->getComponent<Cloud>())if(ImGui::CollapsingHeader("Volumetric clouds")) {
                auto value=cloud->settings();
                ImGui::Checkbox("Enable clouds",&value.enabled);
                ImGui::Checkbox("Cloud temporal reconstruction",&value.temporal);
                ImGui::Checkbox("Immersive voxel cloud",&value.voxel);
                if(value.voxel){
                    ImGui::Checkbox("Empty-space distance skipping",&value.distanceSkipping);
                    ImGui::Checkbox("Homogeneous core integration",&value.coreIntegration);
                    int res=value.voxelResolution==128?1:0;
                    if(ImGui::Combo("Voxel density resolution",&res,"64 cubed\0 128 cubed\0"))value.voxelResolution=res?128:64;
                    ImGui::DragFloat3("Cloud volume center (m)",&value.volumeCenter.x,10);
                    ImGui::SliderFloat3("Cloud volume size (m)",&value.volumeSize.x,100,50000);
                    ImGui::SliderFloat("Storm vortex",&value.storm,0,1);
                    ImGui::SliderFloat("Internal lightning",&value.lightning,0,100);
                }
                int quality=value.downsample==1?0:value.downsample==2?1:2;
                if(ImGui::Combo("Cloud resolution",&quality,"Full\0Half\0Quarter\0"))value.downsample=1u<<quality;
                if(environment_&&environmentReady_&&environment_->snapshot().enabled)
                    ImGui::TextWrapped("Cloud coverage is driven by Environment over 6 s.");
                else ImGui::SliderFloat("Cloud coverage",&value.coverage,0,1);
                ImGui::SliderFloat("Cloud extinction (1/m)",&value.density,0,.01f,"%.4f");
                ImGui::SliderFloat("Cloud base altitude (m)",&value.baseHeight,100,15000);
                ImGui::SliderFloat("Cloud thickness (m)",&value.thickness,100,10000);
                ImGui::SliderFloat("Cloud erosion",&value.erosion,0,1);
                if(environment_&&environmentReady_&&environment_->snapshot().enabled)
                    ImGui::TextWrapped("Cloud wind is driven by Environment.");
                else ImGui::SliderFloat2("Cloud wind (m/s)",&value.wind.x,-100,100);
                int steps=int(value.steps);if(ImGui::SliderInt("Cloud ray steps",&steps,16,192))value.steps=uint32_t(steps);
                try{cloud->setSettings(value);}catch(const std::exception& error){loadError_=error.what();}
            }

			//ImGui::SliderFloat("RayLeigh Scattering",0.0e-3,)
		}
		if (scene->terrain()) {
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Terrain")) {
				auto&& terrainComp = std::static_pointer_cast<TerrainComponent>(scene->terrain()->GetComponent("TerrainComponent"));
				static bool useWireFrame = false;
				ImGui::Toggle("Wire Frame mode", &useWireFrame);
				if (useWireFrame) {
					terrainComp->setPolyMode(GL_LINE);
				}
				else {
					terrainComp->setPolyMode(GL_FILL);
				}
			}
		}
		// ocean
        if(nativeUi_&&scene->terrain())if(auto grass=scene->terrain()->getComponent<Grass>())if(ImGui::CollapsingHeader("Vegetation")){
            auto settings=grass->settings();ImGui::SliderFloat("Plant density",&settings.density,0,1);ImGui::Checkbox("Species distance LOD",&settings.speciesLod);
            ImGui::Checkbox("Tree branch and leaf wind",&settings.treeWind.enabled);
            if(settings.treeWind.enabled){ImGui::SliderFloat("Branch sway",&settings.treeWind.branches,0,2);ImGui::SliderFloat("Leaf flutter",&settings.treeWind.flutter,0,2);if(!settings.environmentWind)ImGui::SliderFloat2("Tree wind XZ (m/s)",&settings.treeWind.velocity.x,-20,20);}
            const char* names[]={"Meadow grass","Tall grass","Reed","Daisy","Wildflower","Broadleaf tree","Conifer","Shrub"};
            for(size_t i=0;i<settings.species.size();i++){auto& v=settings.species[i];ImGui::PushID(int(i));if(ImGui::TreeNode(names[uint32_t(v.type)])){ImGui::SliderFloat("Density",&v.density,0,1);if(uint32_t(v.type)>=5){int variants=int(v.variants);if(ImGui::SliderInt("Seeded shapes",&variants,1,8))v.variants=uint32_t(variants);}
                ImGui::SliderFloat("Height (m)",&v.height,.05f,20);ImGui::SliderFloat("Near spacing (m)",&v.spacing,.05f,12);
                bool taper=v.farSpacing>0;if(ImGui::Checkbox("Distance density",&taper))v.farSpacing=taper?v.spacing*2:0;
                if(taper){v.farSpacing=std::max(v.spacing,v.farSpacing);ImGui::SliderFloat("Far spacing (m)",&v.farSpacing,v.spacing,std::max(20.f,v.spacing));ImGui::SliderFloat("Dense radius (m)",&v.denseRadius,0,std::max(0.f,std::min(v.distance-.1f,v.sparseRadius-.1f)));ImGui::SliderFloat("Sparse radius (m)",&v.sparseRadius,v.denseRadius+.1f,std::max(v.distance,v.denseRadius+.1f));ImGui::SliderFloat("Far coverage width",&v.farWidthScale,1,4);}
                ImGui::SliderFloat("LOD distance (m)",&v.lodDistance,5,std::min(150.f,v.distance-1));v.lodTransition=std::min(v.lodTransition,v.lodDistance);ImGui::TreePop();}ImGui::PopID();}grass->setSettings(settings);
        }
		if (scene->terrain() && scene->terrain()->GetComponent("Ocean") != nullptr) {
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Ocean")) {
				auto&& oceanComp = std::static_pointer_cast<Ocean>(scene->terrain()->GetComponent("Ocean"));

				auto oceanSettings=oceanComp->settings();
                ImGui::Checkbox("Animate waves", &oceanSettings.animate);
                if(environment_&&environmentReady_&&environment_->snapshot().enabled)
                    ImGui::TextWrapped("Wave wind is driven by Environment; Gaussian seed and phase are preserved.");
                else {ImGui::SliderFloat("Wind speed (m/s)", &oceanSettings.WindScale, 0, 40);
                    ImGui::SliderFloat2("Wind direction", &oceanSettings.WindAndSeed.x, -1, 1);}
                ImGui::SliderFloat("Choppiness", &oceanSettings.Lambda, 0, 2);
                if(nativeUi_)ImGui::Checkbox("JONSWAP wind + independent swell", &oceanSettings.seaState.enabled);
                if(oceanSettings.seaState.enabled){
                    auto& sea=oceanSettings.seaState;
                    ImGui::SliderFloat("Wind significant height (m)", &sea.windHeight, 0, 8);
                    ImGui::SliderFloat("Wind peak period (s)", &sea.windPeriod, 2, 12);
                    ImGui::SliderFloat("Wind peak enhancement", &sea.windGamma, 1, 7);
                    ImGui::SliderFloat("Wind directional spreading", &sea.windSpread, 0, 50);
                    ImGui::SliderFloat("Ocean swell significant height (m)", &sea.swellHeight, 0, 8);
                    ImGui::SliderFloat("Ocean swell peak period (s)", &sea.swellPeriod, 2, 16);
                    ImGui::SliderFloat("Ocean swell directional spreading", &sea.swellSpread, 0, 80);
                    float angle=glm::degrees(std::atan2(sea.swellDirection.y,sea.swellDirection.x));
                    if(ImGui::SliderFloat("Ocean swell direction (XZ degrees)", &angle, -180, 180))sea.swellDirection={std::cos(glm::radians(angle)),std::sin(glm::radians(angle))};
                    ImGui::SliderFloat("Uniform wave depth (0 = deep)", &sea.depth, 0, 200);if(sea.depth>0&&sea.depth<.1f)sea.depth=.1f;
                    ImGui::TextWrapped("Wave energy uses significant height and period. Wind drives direction; speed controls the short ripple band.");
                }else ImGui::SliderFloat("Spectrum amplitude", &oceanSettings.A, 0, .003f, "%.6f");
                ImGui::InputInt("Wave seed", &oceanSettings.seed);
                ImGui::InputFloat("Sea level", &oceanSettings.seaLevel);
                ImGui::Text("FFT: %d x %d | mesh: %d x %d", oceanSettings.fft_size, oceanSettings.fft_size, oceanSettings.MeshSize, oceanSettings.MeshSize);
                if(nativeUi_){
                    ImGui::Checkbox("Focus wave mesh near camera", &oceanSettings.cameraGrid);
                    if(oceanSettings.cameraGrid)ImGui::SliderFloat("Mesh focus radius (m)", &oceanSettings.gridFocus, 1, 32);
                    ImGui::Checkbox("Capture underwater surfaces", &oceanSettings.underwaterCapture);
                    ImGui::Checkbox("Integrate water volume", &oceanSettings.volumeIntegration);
                    ImGui::Checkbox("Underwater view + total reflection", &oceanSettings.underwaterView);
                    ImGui::Checkbox("Underwater distance fog", &oceanSettings.underwaterFog);
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("Uses the same absorption/scattering coefficients. Water-side view fog is applied once per optical path.");
                    ImGui::Checkbox("Water scene reflection", &oceanSettings.sceneReflection);
                    ImGui::Checkbox("Wide air refraction", &oceanSettings.underwaterWideRefraction);
                    ImGui::Checkbox("Underwater sun shafts", &oceanSettings.underwaterSunShafts);
                    if(oceanSettings.underwaterSunShafts)ImGui::SliderFloat("Sun shaft contrast", &oceanSettings.sunShaftStrength, 0, 3);
                    bool halfVolume=oceanSettings.underwaterVolumeDownsample==2;
                    if(ImGui::Checkbox("Adaptive half-resolution underwater scattering", &halfVolume))oceanSettings.underwaterVolumeDownsample=halfVolume?2:1;
                    ImGui::SliderInt("Underwater volume samples", &oceanSettings.underwaterVolumeSteps, 4, 32);
                    ImGui::Checkbox("Suspended underwater particles", &oceanSettings.underwaterParticles);
                    if(oceanSettings.underwaterParticles)ImGui::SliderFloat("Particle density", &oceanSettings.particleDensity, 0, 1);
                    ImGui::Checkbox("Robust refraction + terrain fallback", &oceanSettings.robustRefraction);
                    ImGui::Checkbox("Multiple scattering (slab LUT)", &oceanSettings.multipleScattering);
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("Local 2+ volume scattering. Requires Integrate water volume; spatial BSSRDF is not included.");
                    ImGui::Checkbox("Wave particles",&oceanSettings.waveParticles.enabled);
                    if(oceanSettings.waveParticles.enabled){
                        auto& w=oceanSettings.waveParticles;
                        ImGui::SliderFloat("Particle wave speed (m/s)",&w.speed,.5f,10);
                        ImGui::SliderFloat("Wave damping",&w.damping,0,1);
                        ImGui::Checkbox("Reflect at rectangular water bounds",&w.reflectBounds);
                        if(ImGui::Button("Disturb water ahead")){
                            auto camera=scene->mainCamera();auto origin=camera->getPosition(),direction=camera->getFront();
                            float travel=direction.y<-.01f?(oceanSettings.seaLevel-origin.y)/direction.y:10.f;
                            auto at=origin+direction*std::clamp(travel,1.f,40.f);
                            uint64_t id=1;for(auto& q:w.impulses)id=std::max(id,q.id+1);
                            if(w.impulses.size()>=256)w.impulses.erase(w.impulses.begin());
                            w.impulses.push_back({id,double(simulationSeconds_*oceanSettings.TimeScale)+1./60.,{at.x,at.z},{1,0},1.f,1.8f,6.2831853f});
                        }
                        ImGui::TextWrapped("Compact wavefronts and prescribed wakes. Rectangular reflection bounds; no breaking or rigid-body fluid coupling.");
                    }
                    ImGui::Checkbox("Nearshore shallow waves", &oceanSettings.shore.enabled);
                    ImGui::Checkbox("Persistent shore foam", &oceanSettings.shore.foam);
                    ImGui::Checkbox("Wet sand + drying", &oceanSettings.shore.wetSand);
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("Foam and wet sand use Nearshore shallow waves history and require terrain bathymetry.");
                    if(oceanSettings.shore.enabled){
                        ImGui::SliderFloat("Shore patch extent (m)", &oceanSettings.shore.length, 64, 256);
                        int grid=int(oceanSettings.shore.resolution);
                        const char* grids[]={"64","128","256"};int selected=grid<=64?0:grid<=128?1:2;
                        if(ImGui::Combo("Shore simulation grid", &selected,grids,3))oceanSettings.shore.resolution=64u<<selected;
                        ImGui::SliderFloat("Swell height (m)", &oceanSettings.shore.swellHeight, 0, 1);
                        ImGui::SliderFloat("Swell period (s)", &oceanSettings.shore.swellPeriod, 2, 12);
                        ImGui::SliderFloat2("Swell direction", &oceanSettings.shore.swellDirection.x, -1, 1);
                        ImGui::SliderFloat("Shore foam strength", &oceanSettings.shore.foamStrength, 0, 4);
                        ImGui::SliderFloat("Foam lifetime (s)", &oceanSettings.shore.foamLifetime, 1, 20);
                        ImGui::SliderFloat("Sand drying time (s)", &oceanSettings.shore.dryingTime, 5, 60);
                        if(!scene->terrain()->getComponent<TerrainComponent>())ImGui::TextWrapped("Nearshore waves need a terrain height field. Open coastal-beach for a controlled shoreline.");
                    }
                    const char* debugModes[]={"Shaded","Transmittance","Path length","Refraction hit","Hit source: screen / terrain / miss","Hit confidence"};
                    int debug=int(oceanSettings.opticalDebug);if(ImGui::Combo("Water diagnostic", &debug,debugModes,6))oceanSettings.opticalDebug=uint32_t(debug);
                }
                ImGui::Checkbox("Small FFT waves", &oceanSettings.detailWaves);
                ImGui::SliderFloat("Small wave detail", &oceanSettings.detailStrength, 0, 2);
                if(rhi::usesNativeRenderer()) {
                    ImGui::Checkbox("FFT seabed caustics", &oceanSettings.bedCaustics);
                    if(oceanSettings.bedCaustics){
                        ImGui::SliderFloat("Caustic strength", &oceanSettings.causticStrength, 0, 1);
                        ImGui::Checkbox("Near / mid / far caustics", &oceanSettings.causticCascades);
                        ImGui::Checkbox("Caustics on submerged meshes", &oceanSettings.causticMeshReceivers);
                    }
                    ImGui::Checkbox("Short wave ripples", &oceanSettings.shortWaveRipples);
                    if(oceanSettings.shortWaveRipples)ImGui::SliderFloat("Ripple RMS height (m)", &oceanSettings.rippleRmsHeight, 0, .06f, "%.3f");
                }
                ImGui::BeginDisabled(oceanSettings.surfaceModel.physical);
                ImGui::Checkbox("Water refraction", &oceanSettings.refraction);
                ImGui::EndDisabled();
                ImGui::SliderFloat("Refraction strength", &oceanSettings.refractionStrength, 0, 1);
                ImGui::SliderFloat("Water optical range (m)", &oceanSettings.deepWaterDistance, 1, 200);
                ImGui::SliderFloat3("Absorption (1/m)", &oceanSettings.absorption.x, 0, 1);
                ImGui::SliderFloat3("Scattering (1/m)", &oceanSettings.scattering.x, 0, .3f);
                ImGui::SliderFloat("Water scattering strength", &oceanSettings.subsurfaceStrength, 0, 3);
                ImGui::SliderFloat("Forward scattering g", &oceanSettings.scatteringAnisotropy, 0, .9f);
				ImGui::InputFloat("BubblesScale", &oceanSettings.BubblesScale);
				ImGui::InputFloat("BubblesThreshold", &oceanSettings.BubblesThreshold);
				ImGui::InputFloat("TimeScale", &oceanSettings.TimeScale);

				if(nativeUi_&&ImGui::Checkbox("Physical water surface (shared with PT)",&oceanSettings.surfaceModel.physical)&&oceanSettings.surfaceModel.physical)oceanSettings.refraction=true;
                if(oceanSettings.surfaceModel.physical){
                    ImGui::SliderFloat("Water surface roughness",&oceanSettings.surfaceModel.roughness,0,1);
                    ImGui::TextWrapped("IOR 1.333; roughness below 0.02 is smooth. Reflected sun uses physical radiance; sky/volume filtering remains approximate.");
                }else ImGui::SliderFloat("FresnelScale", &oceanSettings.outer_FresnelScale, 0.0f, 1.0f);
				ImGui::SliderFloat("HeightScale", &oceanSettings.HeightScale, 0.0f, 20.0f);
				ImGui::InputFloat3("OceanColorShallow", (float*)&oceanSettings.outer_OceanColorShallow);
				ImGui::InputFloat3("OceanColorDeep", (float*)&oceanSettings.outer_OceanColorDeep);
				ImGui::SliderFloat3("BubblesColor", (float*)&oceanSettings.outer_BubblesColor, 0.0f, 1.0f);
				if(!oceanSettings.surfaceModel.physical)ImGui::SliderFloat3("Specular", (float*)&oceanSettings.outer_Specular, 0.0f, 1.0f);
				if(!oceanSettings.surfaceModel.physical)ImGui::SliderInt("Gloss", &oceanSettings.outer_Gloss, 0, 512);
				ImGui::SliderFloat3("ambient", (float*)&oceanSettings.outer_ambient, 0.0f, 1.0f);
                try{oceanComp->setSettings(oceanSettings);}catch(const std::exception& error){loadError_=error.what();}
			}
		}

		// object list
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Game Objects")) {
			ImGui::BeginChild("Scrolling");
			for (auto& object : scene->objects()) {
				ImGui::Text("%s", object->name.c_str());
			}
			if (scene->sky()) {
				ImGui::Text("sky");
			}
			if (scene->terrain()) {
				ImGui::Text("terrain");
			}
			ImGui::EndChild();
		}

        if(nativeRuntimeStats_)ImGui::Text("Rendering: %.1f FPS",nativeRenderFps_);
        else ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",1000.0f/ImGui::GetIO().Framerate,ImGui::GetIO().Framerate);
		ImGui::End();

		{
			// file dialog
			fileDialog.Display();
			if (fileDialog.HasSelected()) {
				std::string selected = fileDialog.GetSelected().string();
				// 
				//std::cout << selected << '\n';
                if(nativeUi_){if(loading_)loading_->cancel();if(preparing_){preparing_->cancel();preparing_.reset();stagedScene_.reset();}loading_=Loader::GetInstance()->buildScene(selected);environmentLoadingKey_="file:"+std::filesystem::absolute(selected).lexically_normal().string();loadError_.clear();}
                else Loader::GetInstance()->loadSceneAsync(scene, selected);
				fileDialog.ClearSelected();
				//fileDialog.Close();
			}
		}

		ImGui::Render();
	}

	void render() {
        if(nativeUi_){if(!nativeRenderer_)throw std::logic_error("Native UI GPU renderer belongs to render thread");nativeRenderer_->render(ImGui::GetDrawData(),RenderManager::GetInstance()->output());return;}
		#ifdef SCENERENDERER_LEGACY_METAL
        MetalBackend::guiRender(ImGui::GetDrawData());
#elif !defined(SCENERENDERER_METAL)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
	}
};
