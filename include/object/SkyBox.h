#pragma once
#include<string>
#include<vector>
// #include<shader/Shader.h>
#include"../utils/Shader.h"
#include<memory>
#include"../component/GameObject.h"
#include<json/json.hpp>
using json = nlohmann::json;

class RenderManager;
class Material;
enum class ShaderType;
class Component;
class Atmosphere;


class SkyBox:public GameObject{
public:
	unsigned int VAO, VBO;
	std::shared_ptr<Material> material;
	std::vector<std::string> faces;
	std::shared_ptr<Shader> shader;

public:
	SkyBox(); 
	~SkyBox();
	std::shared_ptr<SkyBox> addShader(ShaderType st);
	std::shared_ptr<SkyBox> addMaterial(const std::string& path);
	void init();
	
	void render() const;
};

class Sky:public GameObject{
public:
	Sky();
	~Sky();

	void loadSkyBox(const std::string& filename);
	void render(const std::shared_ptr<Shader>& shader);
	void loadFromJson(json& data);

public:
	//std::shared_ptr<Atmosphere> atmosphere;
	std::shared_ptr<Material> skybox;
	unsigned char* data[6];
	int width, height;
private:
	void initSkyBoxTexture();
};
