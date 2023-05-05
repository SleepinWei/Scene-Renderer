#pragma once
#include<map>
#include<vector>
#include<string>
#include<functional>
#include<exception>

using std::string;
class ClassMeta; 

class Reflection {
private:
	std::map<string, ClassMeta*> meta; 

private:
	Reflection();
	~Reflection();
	Reflection(const Reflection& _ref);
	const Reflection& operator=(const Reflection& _ref);

public:
	static Reflection& getInstance(); 
	ClassMeta* getMetaByClassName(string name);
	void addMeta(string className, ClassMeta* meta);
};


class ClassMeta{
public:
	ClassMeta() = default;
	
	template<typename varType> 
	varType getVarByName(void* instance, string varName) {
		if (vars.find(varName) == vars.end()) {
			// not found
			throw std::runtime_error("member variable is not found");
		}

		return *((varType*)((char*)instance + vars[varName]));
	}

	void* createInstance() {
		return createFunc();
	}

	void setConstructor(void* (*p)(void)) {
		this->createFunc = p; 
	}

//private:
public:
	string name; // class name 
	std::map<string, int> vars;  // variable offset
	std::function<void* (void)> createFunc;

};

#define REFLECTION() static ClassMeta* Register();\
	static void* createInstance();

#define REFLECTION_BEGIN(className) void* className::createInstance(){\
	return new className();\
	}\
	ClassMeta* className::Register(){\
	ClassMeta* newMeta = new ClassMeta();\
	auto& vars = newMeta->vars;\
	newMeta->setConstructor(className::createInstance);

#define REGISTER(classname,var) vars.insert({#var,offsetof(##classname,##var)})

#define REFLECTION_END() \
	return newMeta;\
	}

