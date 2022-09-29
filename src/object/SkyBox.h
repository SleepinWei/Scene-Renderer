#pragma once
#include<string>
#include<vector>
// #include<shader/Shader.h>
#include"../utils/Shader.h"
#include<memory>
#include"../component/GameObject.h"

class RenderManager;
class Material;
enum class ShaderType;


class SkyBox:public GameObject {
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
