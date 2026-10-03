#include"system/InputManager.h"
#include<glfw/glfw3.h>
#include <algorithm>

void InputManager::reset() {
	//for (int i = 0; i < keyStatus.size(); i++) {
		//keyStatus[i] = RELEASED;
	//}
	mouseMove = false;
    deltaX=deltaY=0;mouseScrollX=mouseScrollY=0;
	scrollMove = false;
	viewPortChange = false;
}

InputManager::InputManager() {
	keyStatus = std::vector<int>(KEY_NUM,0);
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
engine::InputFrame InputManager::capture() const {
    engine::InputFrame result;
    const int keys[6]={KEY_W,KEY_S,KEY_A,KEY_D,KEY_E,KEY_Q};
    for(int i=0;i<6;++i)result.movement[i]=keyStatus.at(keys[i])==PRESSED;
    result.movementScale=keyStatus.at(KEY_SHIFT)==PRESSED?5.f:keyStatus.at(KEY_ALT)==PRESSED?.3f:1.f;
    result.mouseMoved=!cursorEnbaled && mouseMove;result.mouseX=deltaX;result.mouseY=deltaY;
    result.scrolled=scrollMove && keyStatus.at(MOUSE_SCROLL)==PRESSED;result.scrollY=float(mouseScrollY);
    result.viewportChanged=viewPortChange;result.width=uint32_t(std::max(0,width));result.height=uint32_t(std::max(0,height));return result;
}
