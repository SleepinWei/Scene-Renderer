#include"system/InputManager.h"
#include<glfw/glfw3.h>

void InputManager::reset() {
	//for (int i = 0; i < keyStatus.size(); i++) {
		//keyStatus[i] = RELEASED;
	//}
	mouseMove = false;
	scrollMove = false;
	viewPortChange = false;
}

InputManager::InputManager() {
	keyStatus = std::vector<int>(20,0);
	cursorEnbaled = true;

	lastX = 0;
	lastY = 0;
	deltaX = 0;
	deltaY = 0;

	mouseScrollX = 0;
	mouseScrollY = 0;
	mouseMove = false;
	scrollMove = false;

	lastFrame = 0;
	deltaFrame = 0;

	width = 1600;
	height = 900;
	viewPortChange = false;
}

void InputManager::tick() {
	float currentFrame = static_cast<float>(glfwGetTime());
	deltaFrame = currentFrame - lastFrame;
	lastFrame = currentFrame;
}

InputManager::~InputManager() {

}

void InputManager::setMousePos(float x,float y){
	this->lastX = x; 
	this->lastY = y;
}

void InputManager::setMouseScroll(float x, float y) {
	this->mouseScrollX = x;
	this->mouseScrollY = y;
}

int InputManager::getKeyStatus(int key) {
	return keyStatus[key];
}