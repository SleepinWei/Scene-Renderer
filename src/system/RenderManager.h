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
class RenderScene;

class RenderManager {
public:
	RenderManager();
	~RenderManager();
	void updateShader(const Camera& camera); 

	void render(std::shared_ptr<RenderScene> renderScene);

	std::shared_ptr<Shader> getShader(ShaderType type); 

private:
	static std::shared_ptr<Shader> generateShader(ShaderType type);

public:
	const int ShaderTypeNum = 5;
	std::vector<std::shared_ptr<Shader>> m_shader;

	// Render Pass
};

