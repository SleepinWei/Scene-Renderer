#pragma once
#include<memory>
#include<vector>
#include<mutex>

class Camera;
class UniformBuffer;
class GameObject;
class RenderScene;
class Shader;
class HDRPass;
class BasePass;
class DepthPass;
class DeferredPass;
class RSMPass;

enum class ShaderType {
	SIMPLE = 0,
	LIGHT,
	PBR,
	PBR_TESS,
	PBR_CLEARCOAT,
	PBR_ANISOTROPY,
	PBR_SSS,
	TERRAIN,
	SKYBOX,
	HDR,
	SKY,
	TEST,
	DEPTH,
	
	KIND_COUNT
};

struct RenderSetting {
	bool enableHDR;
	bool useDefer;
};

class RenderManager {
public:
	RenderManager();
	~RenderManager();

	void init();
	void render(const std::shared_ptr<RenderScene>& scene);

	std::shared_ptr<Shader> getShader(ShaderType type);

private:
	// shader 
	static std::shared_ptr<Shader> generateShader(ShaderType type);
	// buffer
	void prepareVPData(const std::shared_ptr<RenderScene>& renderScene);
	void preparePointLightData(const std::shared_ptr<RenderScene>& renderScene);
	void prepareDirectionLightData(const std::shared_ptr<RenderScene>& renderScene);
	void prepareSpotLightData(const std::shared_ptr<RenderScene>& renderScene);
	void prepareCompData(const std::shared_ptr<RenderScene>& scene);
	void cameraCulling(const std::shared_ptr<RenderScene>& scene);

	void initRenderPass();
	void initVPbuffer();
	void initPointLightBuffer();
	void initDirectionLightBuffer();
	void initSpotLightBuffer();
public:
	// shaders
	std::vector<std::shared_ptr<Shader>> m_shader;

	// setting
	RenderSetting setting;

	// RenderPass
	std::shared_ptr<HDRPass> hdrPass;
	std::shared_ptr<BasePass> basePass;
	std::shared_ptr<DepthPass> depthPass;
	//std::shared_ptr<ShadowPass> shadowPass;
	std::shared_ptr<RSMPass> rsmPass;
	std::shared_ptr<DeferredPass> deferredPass;

	// uniform buffer
	std::shared_ptr<UniformBuffer> uniformVPBuffer;
	std::shared_ptr<UniformBuffer> uniformPointLightBuffer;
	std::shared_ptr<UniformBuffer> uniformDirectionLightBuffer;
	std::shared_ptr<UniformBuffer> uniformSpotLightBuffer;
};

