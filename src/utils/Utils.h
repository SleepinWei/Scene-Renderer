#pragma once
/********************************
* UTILS_H is where all util functions are stored 
* e.g. glfw init/glad init/imgui init 
* e.g. camera control functions(for easier testing) 
* e.g. texutre loading 
* e.g. renderCube 
********************************/

#include "stb/stb_image.h"
#include<iostream>
#include<cmath>
#include"Camera.h"
#include"../utils/Shader.h"

/*******************************
* extern variables 
********************************/
//extern Camera camera;
extern const unsigned int SCR_WIDTH;
extern const unsigned int SCR_HEIGHT;

/********************************
* function statement
********************************/
struct GLFWwindow; 

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window,double xpos,double ypos);
void mouse_button_callback(GLFWwindow* window, int, int, int);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
unsigned int loadTexture(const char *path);
int createWindow(GLFWwindow*& window, int width, int height, std::string title = "Demo");
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void renderCube();
void renderQuad();
int gladInit();
using GLenum = unsigned int;
GLenum glCheckError_(const char* file, int line);
#define glCheckError() glCheckError_(__FILE__, __LINE__) 
