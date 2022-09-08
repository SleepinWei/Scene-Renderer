#pragma once
#include<iostream>

class A {
public:
	A() {
		x = 1;
	}
	virtual void func() {
		std::cout << x << '\n';
	}
	int x; 
};

class B : virtual public A {
public:
	B() {
	}
	void func(int y) {
		std::cout << y<< '\n';
	}
};

