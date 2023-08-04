#pragma once
#include<vector>
#include"Key.h"

class InputManager {
private:
	InputManager();
	~InputManager();
public:
	static InputManager *GetInstance() { 
		static InputManager inputManager;
		return &inputManager;
	}
	void reset(); 
	void setMousePos(float x, float y);
	void setMouseScroll(float x, float y);
	void tick();
	int getKeyStatus(int key);

public:	
	std::vector<int> keyStatus; //1 : pressed 

	bool cursorEnbaled; 

	// mouse 
	float lastX;
	float lastY;
	float deltaX;
	float deltaY;
	bool mouseMove; 
	bool scrollMove;

	double mouseScrollY; 
	double mouseScrollX;

	// frame (time) 
	float lastFrame; 
	float deltaFrame;

	// viewport 
	int width; 
	int height; 
	bool viewPortChange; 
};
