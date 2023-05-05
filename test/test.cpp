#include "pch.h"
#include"utils/Reflection.h"
#include<iostream>
using std::cout;

class TestClass {
public:
	int a; 
	float b; 
	int c; 
public:
	TestClass() {
		a = 1;
		b = 2; 
		c = 3;
	}
	void func() {

	}
	REFLECTION();
};

REFLECTION_BEGIN(TestClass)
REGISTER(TestClass, a);
REGISTER(TestClass, b);
REGISTER(TestClass, c);
REFLECTION_END()

TEST(ReflectionTest, ReflectionVariable) {
	Reflection& instance = Reflection::getInstance();
	ClassMeta* newMeta = TestClass::Register();
	//newMeta->setConstructor(TestClass::createInstance);
	TestClass* newInst =(TestClass*)newMeta->createInstance();

	for (auto iter : newMeta->vars) {
		std::cout << iter.first << ' ' << iter.second << '\n';
	}
	std::cout << typeid(newInst).name() << "\n";
	delete newMeta;
}

int main(int argc, char** argv) {
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}