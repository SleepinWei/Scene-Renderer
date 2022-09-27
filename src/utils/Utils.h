#pragma once
/********************************
* UTILS_H is where all util functions are stored 
* e.g. glfw init/glad init/imgui init 
* e.g. camera control functions(for easier testing) 
* e.g. texutre loading 
* e.g. renderCube 
********************************/

#include "stb/stb_image.h"
#include"Camera.h"
#include<iostream>
#include<glfw/glfw3.h>
//#include"Vehicle.h"
//#include<imgui/imgui.h>
//#include<imgui/imgui_impl_glfw.h>
//#include<imgui/imgui_impl_opengl3.h>
#include<cmath>
//#include<imgui/imgui_impl_opengl3_loader.h>
#include"../utils/Shader.h"

/*******************************
* extern variables 
********************************/
extern Camera camera;
extern const unsigned int SCR_WIDTH;
extern const unsigned int SCR_HEIGHT;

extern float lastX;
extern float lastY;
extern bool firstMouse;

// time 
extern float deltaTime;
extern float lastFrame;

//cubes 
//extern unsigned int cubeVAO;
//extern unsigned int cubeVBO;
//extern unsigned int quadVAO;
//extern unsigned int quadVBO;
//
//extern unsigned int planeVAO;
/********************************
* function statement
********************************/
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);
unsigned int loadTexture(const char *path);
int createWindow(GLFWwindow*& window, int width, int height, std::string title = "Demo");
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
unsigned int createCube();
int gladInit();
//void imGuiInit(GLFWwindow* window);
//void renderCube();

/********************************
* function impelementatations
********************************/
//void imGuiInit(GLFWwindow* window) {
//    //imgui
//    IMGUI_CHECKVERSION();
//    ImGui::CreateContext();
//    ImGuiIO& io = ImGui::GetIO(); (void)io;
//    //enable keyboard controls 
//    //enable gamepad controls 
//    ImGui::StyleColorsDark();
//
//    ImGui_ImplGlfw_InitForOpenGL(window, true);
//    ImGui_ImplOpenGL3_Init("#version 150");
//}


// !UTILS_H
