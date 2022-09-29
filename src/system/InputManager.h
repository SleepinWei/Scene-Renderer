#pragma once
#include<vector>
#include"Key.h"

class InputManager {

public:
	InputManager();
	~InputManager();
	void reset(); 
	void setMousePos(float x, float y);
	void setMouseScroll(float x, float y);
	void setFrame();//TODO
	int getKeyStatus(int key);

public:	
	std::vector<int> keyStatus; //1 : pressed 

	bool cursorEnbaled; 

	// mouse 
	float lastX;
	float lastY;
	float deltaX;
	float deltaY;
	double mouseScrollY; 
	double mouseScrollX;

	float lastFrame; 
	float deltaFrame;
};
