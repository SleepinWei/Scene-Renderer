#pragma once
#include <memory>
#include "engine/RenderSettings.h"
#include <mutex>
#include <vector>

#include "rhi/GraphicsDevice.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
namespace render{class SceneAdapter;class ForwardPbrRenderer;}
class Camera;
class UniformBuffer;
class GameObject;
class RenderScene;
class Shader;
class PostPass;
class BasePass;
class DepthPass;
class DeferredPass;
class RSMPass;
class ShadowPass;
class SSAOPass;
class TemporalAA;

class RenderManager
{
private:
	RenderManager();
	~RenderManager();

public:
	static RenderManager *GetInstance()
	{
		static RenderManager renderManager;
		return &renderManager;
	}

	void init();
    bool native()const{return native_;}
    rhi::TextureHandle output()const;
    void releaseNative();
	void render(const std::shared_ptr<RenderScene> &scene);

	// add a tool function to pass the UBO and a cascaded levels from shadow pass to deferred pass.
	void pass_data();

	std::shared_ptr<Shader> getShader(ShaderType type);

private:
	bool native_=false;
    std::unique_ptr<render::SceneAdapter> adapter_;
    std::unique_ptr<render::ForwardPbrRenderer> renderer_;
	// shader
	static std::shared_ptr<Shader> generateShader(ShaderType type);
	// buffer
	void prepareVPData(const std::shared_ptr<RenderScene> &renderScene);
	void preparePointLightData(const std::shared_ptr<RenderScene> &renderScene);
	void prepareDirectionLightData(const std::shared_ptr<RenderScene> &renderScene);
	void prepareSpotLightData(const std::shared_ptr<RenderScene> &renderScene);
	void prepareCompData(const std::shared_ptr<RenderScene> &scene);
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
	std::shared_ptr<PostPass> postPass;
	std::shared_ptr<BasePass> basePass;
	std::shared_ptr<DepthPass> depthPass;
	// std::shared_ptr<ShadowPass> shadowPass;
	std::shared_ptr<RSMPass> rsmPass;

	std::shared_ptr<ShadowPass> shadowPass;
	std::shared_ptr<DeferredPass> deferredPass;
	std::shared_ptr<SSAOPass> ssaoPass;
    std::shared_ptr<TemporalAA> temporalAA;

	// uniform buffer
	std::shared_ptr<UniformBuffer> uniformVPBuffer;
	std::shared_ptr<UniformBuffer> uniformPointLightBuffer;
	std::shared_ptr<UniformBuffer> uniformDirectionLightBuffer;
	std::shared_ptr<UniformBuffer> uniformSpotLightBuffer;
};
