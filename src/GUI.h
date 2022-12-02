#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include<imgui/imfilebrowser.h>
// #include<glfw/glfw3.h>
#include<glm/glm.hpp>
#include"system/ModelLoader.h"
#include"renderer/RenderScene.h"
#include"./component/Lights.h"
#include"./component/Transform.h"
#include"./component/Atmosphere.h"
#include"./component/TerrainComponent.h"
#include<imgui/imgui_toggle.h>
#include<filesystem>
using namespace std::filesystem;

extern std::shared_ptr<ModelLoader> modelLoader;

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

		ImGui::Text("Scene loading");
		if (ImGui::Button("Select Scene")) {
			fileDialog.Open();
		}
		ImGui::Separator();
		ImGui::Text("Camera");
		if (scene->main_camera) {
			// exposure
			ImGui::SliderFloat("exposure",&(scene->main_camera->exposure),0.5f,6.0f);
		}

		ImGui::Separator();
		ImGui::Text("Point Light");
		auto& lights = scene->pointLights;
		for (int i = 0; i < lights.size(); i++) {
			char title[] = "Lighti Position";
			title[5] = '0' + i;
			ImGui::Text(title);
			//auto& light = lights[0]; 
			auto&& lightTrans = std::static_pointer_cast<Transform>(
				lights[i]->gameObject->GetComponent("Transform"));
			if(ImGui::SliderFloat3("Position", (float*)&lightTrans->position, -10.0f, 10.0f))
				lights[i]->setDirtyFlag(true);
		}

		//auto& dlights = scene->directionLights;
		//for (int i = 0; i < dlights.size(); i++) {
		//	char title[] = "Direction Lighti Position";
		//	title[15] = '0' + 0;
		//	ImGui::Text(title);
		//	//auto& light = lights[0]; 
		//	auto&& lightTrans = std::static_pointer_cast<Transform>(
		//		dlights[i]->gameObject->GetComponent("Transform"));
		//	ImGui::SliderFloat3("Position", (float*)&lightTrans->position, -10.0f, 1.0f);
		//	dlights[i]->setDirtyFlag(true);
		//}

		// sky 
		if (scene->sky) {
			ImGui::Separator();
			ImGui::Text("Atmosphere");
			auto& atmos = std::static_pointer_cast<Atmosphere>(scene->sky->GetComponent("Atmosphere"));
			auto& atmosParam = atmos->atmosphere;
			auto& sunAngle = atmos->sunAngle;
			ImGui::SliderFloat("sunAngle", &sunAngle, -10.0f, 90.0f);
			ImGui::SliderFloat("mie_g", &atmosParam.mie_g, 0.0f, 1.0f);
			ImGui::SliderFloat3("rayleigh_scattering", (float*) & atmosParam.rayleigh_scattering, 0.0f, 1.0f);

			//ImGui::SliderFloat("RayLeigh Scattering",0.0e-3,)
		}
		if (scene->terrain) {
			ImGui::Separator();
			ImGui::Text("Terrain");
			auto& terrainComp = std::static_pointer_cast<TerrainComponent>(scene->terrain->GetComponent("TerrainComponent"));
			static bool useWireFrame = false;
			ImGui::Toggle("Wire Frame mode", &useWireFrame);
			if (useWireFrame) {
				terrainComp->setPolyMode(GL_LINE);
			}
			else {
				terrainComp->setPolyMode(GL_FILL);
			}
		}

		// object list
		ImGui::Separator();
		ImGui::Text("Game Objects");
		ImGui::BeginChild("Scrolling");
		for (auto& object : scene->objects) {
			ImGui::Text("%s",object->name.c_str());
		}
		if (scene->sky) {
			ImGui::Text("sky");
		}
		if (scene->terrain) {
			ImGui::Text("terrain");
		}
		ImGui::EndChild();

		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();

		{
			// file dialog
			fileDialog.Display();
			if (fileDialog.HasSelected()) {
				std::string selected = fileDialog.GetSelected().string();
				// 
				//std::cout << selected << '\n';
				modelLoader->loadSceneAsync(scene, selected);
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
