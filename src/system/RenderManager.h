#pragma once
#include<memory>
#include<vector>

class Camera;
class UniformBuffer;
class GameObject;
class RenderScene;
class Shader;

enum class ShaderType{
	SIMPLE=0,
	LIGHT,
	PBR,
	TERRAIN,
	SKYBOX
};


class RenderManager {
public:
	RenderManager();
	~RenderManager();
	void prepareVPData(const std::shared_ptr<RenderScene>& renderScene); 
	void prepareLightData(const std::shared_ptr<RenderScene>& renderScene);

	void render(const std::shared_ptr<RenderScene>& renderScene);

	std::shared_ptr<Shader> getShader(ShaderType type); 

private:
	static std::shared_ptr<Shader> generateShader(ShaderType type);

public:
	const int ShaderTypeNum = 5;
	std::vector<std::shared_ptr<Shader>> m_shader;

	// frame buffer 

	// uniform buffer
	std::shared_ptr<UniformBuffer> uniformVPBuffer;
	bool shaderVPdirty;

	// Render Pass
};

