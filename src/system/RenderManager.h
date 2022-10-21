#pragma once
#include<memory>
#include<vector>

class Camera;
class UniformBuffer;
class GameObject;
class RenderScene;
class Shader;
class HDRPass;
class BasePass;

enum class ShaderType{
	SIMPLE=0,
	LIGHT,
	PBR,
	PBR_TESS,
	TERRAIN,
	SKYBOX,
	HDR,
	SKY,
	TEST,
	
	KIND_COUNT
};

struct RenderSetting {
	bool enableHDR;
};

class RenderManager {
public:
	RenderManager();
	~RenderManager();
	void prepareVPData(const std::shared_ptr<RenderScene>& renderScene); 
	void preparePointLightData(const std::shared_ptr<RenderScene>& renderScene);
	void prepareDirectionLightData(const std::shared_ptr<RenderScene>& renderScene);

	void render(const std::shared_ptr<RenderScene>& scene);

	std::shared_ptr<Shader> getShader(ShaderType type); 

private:
	// shader 
	static std::shared_ptr<Shader> generateShader(ShaderType type);
	// buffer
	void initVPbuffer(); 
	void initPointLightBuffer(); 
	void initDirectionLightBuffer();

public:
	// shaders
	std::vector<std::shared_ptr<Shader>> m_shader;

	// setting
	RenderSetting setting;

	// RenderPass
	std::shared_ptr<HDRPass> hdrPass;
	std::shared_ptr<BasePass> basePass;
	//std::shared_ptr<ShadowPass> shadowPass;
	//std::shared_ptr<DeferredPass> deferredPass;

	// uniform buffer
	std::shared_ptr<UniformBuffer> uniformVPBuffer;
	std::shared_ptr<UniformBuffer> uniformPointLightBuffer;
	std::shared_ptr<UniformBuffer> uniformDirectionLightBuffer;
};

