#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include<imgui/imfilebrowser.h>
// #include<glfw/glfw3.h>
#include<glm/glm.hpp>
#include"system/Loader.h"
#include"renderer/RenderScene.h"
#include"component/Lights.h"
#include"component/Transform.h"
#include"component/Atmosphere.h"
#include"component/TerrainComponent.h"
#include"component/Ocean.h"
#include "utils/Camera.h"
#include<imgui/imgui_toggle.h>
#include<filesystem>
#include<system/RenderManager.h>
#include<object/SkyBox.h>
#include<object/Terrain.h>
#include<component/GameObject.h>

using namespace std::filesystem;

class Gui {
public:
	ImGui::FileBrowser fileDialog;
	const std::string base_path = "./asset/objects";
public:
	Gui(GLFWwindow* window) {
		const char* glsl_version = "#version 330";
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsLight();
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init(glsl_version);

		fileDialog.SetTypeFilters({ ".json" });
		fileDialog.SetPwd(base_path);
	}
	void destroy() {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}
	void window(std::shared_ptr<RenderScene>& scene) {
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Info");

		if (ImGui::CollapsingHeader("Scene loading")) {
			if (ImGui::Button("Select Scene")) {
				fileDialog.Open();
			}
		}
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Camera")) {
			if (scene->main_camera) {
				// exposure
				ImGui::SliderFloat("exposure", &(scene->main_camera->exposure), 0.5f, 6.0f);
			}
		}

		if (ImGui::CollapsingHeader("Rendering")) {
			auto &setting = RenderManager::GetInstance()->setting;
			static bool enableShadow = setting.enableShadow;
			if (ImGui::Toggle("Enable Shadow", &enableShadow)) {
				setting.enableShadow = enableShadow;
			}
			static bool enableRSM = setting.enableRSM;
			if (ImGui::Toggle("Enable RSM", &enableRSM)) {
				setting.enableRSM= enableRSM;
			}
			static bool enableDirectional= setting.enableDirectional;
			if (ImGui::Toggle("Enable Directional", &enableDirectional)) {
				setting.enableDirectional= enableDirectional;
			}
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Point Light")) {
			auto& lights = scene->pointLights;
			for (int i = 0; i < lights.size(); i++) {
				char title[] = "Lighti Position";
				title[5] = '0' + i;
				ImGui::Text(title);
				//auto& light = lights[0]; 
				auto&& lightTrans = std::static_pointer_cast<Transform>(
					lights[i]->gameObject->GetComponent("Transform"));
				if (ImGui::SliderFloat3("Position", (float*)&lightTrans->position, -10.0f, 10.0f))
					lights[i]->setDirtyFlag(true);
				//ImGui::Text("Light Position: (%f,%f,%f)", lightTrans->position.x, lightTrans->position.y, lightTrans->position.z);
			}
		}
		if(ImGui::CollapsingHeader("Direction Light")){
			auto& dlights = scene->directionLights;
			for (int i = 0; i < dlights.size(); i++) {
				char title[] = "Direction Lighti";
				title[15] = '0' + 0;
				ImGui::Text(title);
				//auto& light = lights[0]; 
				auto&& lightTrans = std::static_pointer_cast<Transform>(
					dlights[i]->gameObject->GetComponent("Transform"));
				auto& lightData = dlights[i]->data;
				//bool change1 = ImGui::SliderFloat3("Position", (float*)&lightTrans->position, -10.0f, 1.0f);
				bool change1 = false; 
				bool change2 = ImGui::SliderFloat3("Direction", (float*)&lightData.direction, -1.0f, 1.0f);
				// ImGui::Text("Light Position: (%f,%f,%f)", lightTrans->position.x, lightTrans->position.y, lightTrans->position.z);
				// ImGui::Text("Light Direction: (%f,%f,%f)", lightData.direction.x, lightData.direction.y, lightData.direction.z);
			
				if (change1 || change2) {
					dlights[i]->setDirtyFlag(true);
				}
			}
		}
		if (ImGui::CollapsingHeader("Spot Light")) {
			auto& slights = scene->spotLights;
			for (int i = 0; i < slights.size(); i++) {
				char title[] = "Spot Lighti Position";
				title[15] = '0' + 0;
				ImGui::Text(title);
				//auto& light = lights[0]; 
				auto&& lightTrans = std::static_pointer_cast<Transform>(
					slights[i]->gameObject->GetComponent("Transform"));
				auto&& lightData = slights[i]->data;
				//bool change1 = ImGui::SliderFloat3("Position", (float*)&lightTrans->position, -10.0f, 1.0f);
				//bool change2 = ImGui::SliderFloat3("Direction", (float*)&lightData.direction, -1.0f, 1.0f);
				ImGui::Text("Light Position: (%f,%f,%f)", lightTrans->position.x, lightTrans->position.y, lightTrans->position.z);
				ImGui::Text("Light Direction: (%f,%f,%f)", lightData.direction.x, lightData.direction.y, lightData.direction.z);
			
				ImGui::Text("CutOff: %f", lightData.cutOff);
				ImGui::Text("OuterCutOff: %f", lightData.outerCutOff);
				//if (change1 || change2) {
					//slights[i]->setDirtyFlag(true);
				//}
			}
		}

		// sky 
		if (scene->sky) {
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Atmosphere")) {
				auto&& atmos = std::static_pointer_cast<Atmosphere>(scene->sky->GetComponent("Atmosphere"));
				auto& atmosParam = atmos->atmosphere;
				auto& sunAngle = atmos->sunAngle;
				ImGui::SliderFloat("sunAngle", &sunAngle, -10.0f, 90.0f);
				ImGui::SliderFloat("mie_g", &atmosParam.mie_g, 0.0f, 1.0f);
				ImGui::SliderFloat3("rayleigh_scattering", (float*)&atmosParam.rayleigh_scattering, 0.0f, 1.0f);
			}

			//ImGui::SliderFloat("RayLeigh Scattering",0.0e-3,)
		}
		if (scene->terrain) {
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Terrain")) {
				auto&& terrainComp = std::static_pointer_cast<TerrainComponent>(scene->terrain->GetComponent("TerrainComponent"));
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
		if (scene->terrain && scene->terrain->GetComponent("Ocean") != nullptr) {
			ImGui::Separator();
			if (ImGui::CollapsingHeader("Ocean")) {
				auto&& oceanComp = std::static_pointer_cast<Ocean>(scene->terrain->GetComponent("Ocean"));

				ImGui::InputFloat("BubblesScale", &oceanComp->BubblesScale);
				ImGui::InputFloat("BubblesThreshold", &oceanComp->BubblesThreshold);
				ImGui::InputFloat("TimeScale", &oceanComp->TimeScale);

				ImGui::SliderFloat("FresnelScale", &oceanComp->outer_FresnelScale, 0.0f, 1.0f);
				ImGui::SliderFloat("HeightScale", &oceanComp->HeightScale, 0.0f, 20.0f);
				ImGui::InputFloat3("OceanColorShallow", (float*)&oceanComp->outer_OceanColorShallow);
				ImGui::InputFloat3("OceanColorDeep", (float*)&oceanComp->outer_OceanColorDeep);
				ImGui::SliderFloat3("BubblesColor", (float*)&oceanComp->outer_BubblesColor, 0.0f, 1.0f);
				ImGui::SliderFloat3("Specular", (float*)&oceanComp->outer_Specular, 0.0f, 1.0f);
				ImGui::SliderInt("Gloss", &oceanComp->outer_Gloss, 0, 512);
				ImGui::SliderFloat3("ambient", (float*)&oceanComp->outer_ambient, 0.0f, 1.0f);
			}
		}

		// object list
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Game Objects")) {
			ImGui::BeginChild("Scrolling");
			for (auto& object : scene->objects) {
				ImGui::Text("%s", object->name.c_str());
			}
			if (scene->sky) {
				ImGui::Text("sky");
			}
			if (scene->terrain) {
				ImGui::Text("terrain");
			}
			ImGui::EndChild();
		}

		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();

		{
			// file dialog
			fileDialog.Display();
			if (fileDialog.HasSelected()) {
				std::string selected = fileDialog.GetSelected().string();
				// 
				//std::cout << selected << '\n';
				Loader::GetInstance()->loadSceneAsync(scene, selected);
				fileDialog.ClearSelected();
				//fileDialog.Close();
			}
		}

		ImGui::Render();
	}

	void render() {
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
};
