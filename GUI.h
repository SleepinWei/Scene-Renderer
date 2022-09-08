#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include<glfw/glfw3.h>
#include<glm/glm.hpp>

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
	void window(glm::vec3& lightPos) {
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Info");
		ImGui::Text("Light Position");
		ImGui::SliderFloat("X", &lightPos.x, -8.0f, 8.0f);
		ImGui::SliderFloat("Y", &lightPos.y, 0.0f, 8.0f);
		ImGui::SliderFloat("Z", &lightPos.z, -8.0f, 8.0f);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();

		ImGui::Render();
	}

	void render() {
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
};
