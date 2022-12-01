#pragma once
#include<string>
#include<vector>
#include"../component/GameObject.h"
#include<memory>
#include<json/json.hpp>
using json = nlohmann::json;

class Material;
enum class ShaderType; 
class Shader; 
class SSBO;
class ImageTexture;
class Component;

class Terrain :public GameObject {
public:
	Terrain();
	~Terrain();
public:
	void render(const std::shared_ptr<Shader>& shader);
	virtual void loadFromJson(json& data) override;

	void constructCall();
private:
};

