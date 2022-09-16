#pragma once
#include<memory>
#include<vector>
#include<shader/Shader.h>
#include"src/Camera.h"

extern Camera camera;

enum class ShaderType{
	SIMPLE=0,
	LIGHT,
	PBR,
	TERRAIN,
	SKYBOX
};

class Renderable;
class RenderManager;
extern class RenderManager renderManager;

class RenderManager {
public:
	RenderManager();
	~RenderManager();
	void updateShader(const Camera& camera);
//test
	void render(std::vector<std::shared_ptr<Renderable>>& objects,
		glm::vec3,glm::vec3);
	std::shared_ptr<Shader> registerShader(ShaderType type);

	std::shared_ptr<Shader> getShader(ShaderType type); 
	static std::shared_ptr<Shader> generateShader(ShaderType type);

public:
	const int ShaderTypeNum = 5;
	std::vector<std::shared_ptr<Shader>> m_shader;
	//std::vector<int> m_shaderCnt;
};

class Renderable {
	
public:
	virtual void registerShader(ShaderType type) {
		this->shader = renderManager.registerShader(type);
	};
	virtual void render() {} ; 
	/*
	* Material: render() 为shader设置参数
	*/

public:
	std::shared_ptr<Shader> shader;
	//std::vector<std::shared_ptr<Texture>> textures;
};

