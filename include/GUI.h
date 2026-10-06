#include "engine/RenderRuntime.h"
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
    bool pauseSimulation_=false;float simulationSpeed_=1;
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
                else scene->replaceWith(*built);
                loadError_.clear();}catch(const std::exception& error){loadError_=error.what();}
            loading_.reset();
        }
        if(preparing_ && preparing_->ready.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try{preparing_->ready.get();if(preparing_->cancelled->load())throw std::runtime_error("Scene load cancelled");
                scene->replaceWith(*stagedScene_);activateScene_(preparing_->token);loadError_.clear();}
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

		ImGui::Begin("Info");
        ImGui::TextWrapped("Hold right mouse: look | WASD: move | Q/E: down/up");
        ImGui::Text("Framebuffer: %d x %d",InputManager::GetInstance()->width,InputManager::GetInstance()->height);

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
                ImGui::SliderFloat("Shadow distance",&shadows.distance,10,5000);
                ImGui::SliderFloat("Cascade blend",&shadows.cascadeBlend,0,.3f);
                ImGui::SliderFloat("Shadow bias (world units)",&shadows.depthBias,0,.05f,"%.4f");
                ImGui::SliderFloat("Local light radius",&shadows.localLightRadius,0,1);
            }
            ImGui::Toggle("Enable RSM", &setting.enableRSM);
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
            if(nativeUi_)ImGui::Checkbox("Automatic quality under memory pressure",&setting.automaticQuality);
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
				bool change2 = ImGui::SliderFloat3("Direction", (float*)&lightData.direction, -1.0f, 1.0f);
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
				ImGui::SliderFloat("Sun elevation", &sunAngle, -20.0f, 90.0f);
                ImGui::SliderFloat("Sun azimuth", &atmosphereSettings.sunAzimuth, -180.0f, 180.0f);
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
                ImGui::SliderFloat("Cloud coverage",&value.coverage,0,1);
                ImGui::SliderFloat("Cloud extinction (1/m)",&value.density,0,.01f,"%.4f");
                ImGui::SliderFloat("Cloud base altitude (m)",&value.baseHeight,100,15000);
                ImGui::SliderFloat("Cloud thickness (m)",&value.thickness,100,10000);
                ImGui::SliderFloat("Cloud erosion",&value.erosion,0,1);
                ImGui::SliderFloat2("Cloud wind (m/s)",&value.wind.x,-100,100);
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
		if (scene->terrain() && scene->terrain()->GetComponent("Ocean") != nullptr) {
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Ocean")) {
				auto&& oceanComp = std::static_pointer_cast<Ocean>(scene->terrain()->GetComponent("Ocean"));

				auto oceanSettings=oceanComp->settings();
                ImGui::Checkbox("Animate waves", &oceanSettings.animate);
                ImGui::SliderFloat("Wind speed (m/s)", &oceanSettings.WindScale, 0, 40);
                ImGui::SliderFloat2("Wind direction", &oceanSettings.WindAndSeed.x, -1, 1);
                ImGui::SliderFloat("Choppiness", &oceanSettings.Lambda, 0, 2);
                ImGui::SliderFloat("Spectrum amplitude", &oceanSettings.A, 0, .003f, "%.6f");
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
                    ImGui::Checkbox("Robust refraction + terrain fallback", &oceanSettings.robustRefraction);
                    ImGui::Checkbox("Multiple scattering (slab LUT)", &oceanSettings.multipleScattering);
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("Local 2+ volume scattering. Requires Integrate water volume; spatial BSSRDF is not included.");
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
                    ImGui::Checkbox("Short wave ripples", &oceanSettings.shortWaveRipples);
                    if(oceanSettings.shortWaveRipples)ImGui::SliderFloat("Ripple RMS height (m)", &oceanSettings.rippleRmsHeight, 0, .06f, "%.3f");
                }
                ImGui::Checkbox("Water refraction", &oceanSettings.refraction);
                ImGui::SliderFloat("Refraction strength", &oceanSettings.refractionStrength, 0, 1);
                ImGui::SliderFloat("Water optical range (m)", &oceanSettings.deepWaterDistance, 1, 200);
                ImGui::SliderFloat3("Absorption (1/m)", &oceanSettings.absorption.x, 0, 1);
                ImGui::SliderFloat3("Scattering (1/m)", &oceanSettings.scattering.x, 0, .3f);
                ImGui::SliderFloat("Water scattering strength", &oceanSettings.subsurfaceStrength, 0, 3);
                ImGui::SliderFloat("Forward scattering g", &oceanSettings.scatteringAnisotropy, 0, .9f);
				ImGui::InputFloat("BubblesScale", &oceanSettings.BubblesScale);
				ImGui::InputFloat("BubblesThreshold", &oceanSettings.BubblesThreshold);
				ImGui::InputFloat("TimeScale", &oceanSettings.TimeScale);

				ImGui::SliderFloat("FresnelScale", &oceanSettings.outer_FresnelScale, 0.0f, 1.0f);
				ImGui::SliderFloat("HeightScale", &oceanSettings.HeightScale, 0.0f, 20.0f);
				ImGui::InputFloat3("OceanColorShallow", (float*)&oceanSettings.outer_OceanColorShallow);
				ImGui::InputFloat3("OceanColorDeep", (float*)&oceanSettings.outer_OceanColorDeep);
				ImGui::SliderFloat3("BubblesColor", (float*)&oceanSettings.outer_BubblesColor, 0.0f, 1.0f);
				ImGui::SliderFloat3("Specular", (float*)&oceanSettings.outer_Specular, 0.0f, 1.0f);
				ImGui::SliderInt("Gloss", &oceanSettings.outer_Gloss, 0, 512);
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
				if(nativeUi_){if(loading_)loading_->cancel();if(preparing_){preparing_->cancel();preparing_.reset();stagedScene_.reset();}loading_=Loader::GetInstance()->buildScene(selected);loadError_.clear();}
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
