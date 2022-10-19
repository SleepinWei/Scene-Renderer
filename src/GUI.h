#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
// #include<glfw/glfw3.h>
#include<glm/glm.hpp>
#include"renderer/RenderScene.h"
#include"./component/Lights.h"
#include"./component/Transform.h"
#include"./component/Atmosphere.h"

class Gui {
public:
	Gui(GLFWwindow* window) {
		const char* glsl_version = "#version 330";
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsLight();
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init(glsl_version);
	}
	void destroy() {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}
	void window(std::shared_ptr<RenderScene>& scene,float* sunAngle,AtmosphereParameters* parameters) {
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Info");
		auto& lights = scene->pointLights;
		for (int i = 0; i < lights.size(); i++) {
			char title[] = "Lighti Position";
			title[5] = '0' + 0;
			ImGui::Text(title);
			//auto& light = lights[0]; 
			auto&& lightTrans = std::dynamic_pointer_cast<Transform>(
				lights[i]->gameObject->GetComponent("Transform"));
			ImGui::SliderFloat("X", &(lightTrans->position.x), -8.0f, 8.0f);
			ImGui::SliderFloat("Y", &(lightTrans->position.y), -8.0f, 8.0f);
			ImGui::SliderFloat("Z", &(lightTrans->position.z), -8.0f, 8.0f);
			lights[i]->setDirtyFlag(true);
		}
		auto& dlights = scene->directionLights;
		for (int i = 0; i < dlights.size(); i++) {
			char title[] = "Direction Lighti Position";
			title[15] = '0' + 0;
			ImGui::Text(title);
			//auto& light = lights[0]; 
			auto&& lightTrans = std::dynamic_pointer_cast<Transform>(
				dlights[i]->gameObject->GetComponent("Transform"));
			ImGui::SliderFloat("X", &(lightTrans->position.x), -8.0f, 8.0f);
			ImGui::SliderFloat("Y", &(lightTrans->position.y), -8.0f, 8.0f);
			ImGui::SliderFloat("Z", &(lightTrans->position.z), -8.0f, 8.0f);
			dlights[i]->setDirtyFlag(true);
		}

		ImGui::Text("Atmosphere Parameters");
		ImGui::SliderFloat("sunAngle", sunAngle, -10.0f, 90.0f);
		//ImGui::SliderFloat("RayLeigh Scattering",0.0e-3,)

		ImGui::Text("Light Intensity");
		//ImGui::SliderFloat("Intensity", &lightColor, 0.5f, 20.0f);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();

		ImGui::Render();
	}

	void render() {
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
};
