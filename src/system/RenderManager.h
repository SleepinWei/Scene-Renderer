#pragma once
#include<memory>
#include<vector>
#include<shader/Shader.h>
#include"../utils/Camera.h"
#include"../component/GameObject.h"

extern Camera camera;

enum class ShaderType{
	SIMPLE=0,
	LIGHT,
	PBR,
	TERRAIN,
	SKYBOX
};

class RenderManager;
extern class RenderManager renderManager;

class RenderManager {
public:
	RenderManager();
	~RenderManager();
	void updateShader(const Camera& camera);
//test
	void render(std::vector<std::shared_ptr<GameObject>>& objects,
		const glm::vec3 & ,const glm::vec3&);

	std::shared_ptr<Shader> getShader(ShaderType type); 

private:
	static std::shared_ptr<Shader> generateShader(ShaderType type);

public:
	const int ShaderTypeNum = 5;
	std::vector<std::shared_ptr<Shader>> m_shader;
};

