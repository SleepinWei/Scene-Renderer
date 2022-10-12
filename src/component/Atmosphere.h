#pragma once
#include<memory>
#include"./Component.h"

class Atmosphere :public Component,public std::enable_shared_from_this<Atmosphere>{
public:
	Atmosphere();
	~Atmosphere();

public:
	//parameters 
	float Rg; // R ground
	float Rm; // R max // maximum parameter
};
