#pragma once
#include<memory>
#include <vector>
#include <glm\glm.hpp>
#include "../component/Lights.h"
class RenderScene; 
class Shader;
class FrameBuffer;
class Texture;
class RenderBuffer;

using std::shared_ptr;

class HDRPass {
public:
	HDRPass();
	~HDRPass();

	void bindBuffer(); 
	void unbindBuffer();

	void render(); 

private:
	// generate texture of width by height (colorbuffer & render bfufer)
	void initPass(int SCR_WITH,int SCR_HEIGHT);

public:
	std::shared_ptr<Shader> hdrShader;
	bool dirty; 
	unsigned int hdrFBO; // framebuffer object
	unsigned int colorBuffer; // texture
	unsigned int rboDepth; // rendrebuffer object 
};

/// <summary>
/// base pass: renders scene objects to framebuffer in forward-rendering pipeline, 
/// this is not a base class for other render pass classes
/// this class should not have any functions to operate on framebuffers
/// </summary>
class BasePass {
public:
	BasePass() = default; 
	~BasePass() = default;

	void render(const std::shared_ptr<RenderScene>& scene,const std::shared_ptr<Shader>& shader);
public:

};

class DepthPass {
public:
	DepthPass();
	~DepthPass();

	void render(const std::shared_ptr<RenderScene>& scene);
public:
	std::shared_ptr<Shader> depthShader;
	std::shared_ptr<FrameBuffer> frameBuffer; //scene size
	std::shared_ptr<Texture> frontDepth;
	std::shared_ptr<Texture> backDepth;
	bool dirty;
private:
	std::shared_ptr<RenderBuffer> renderBuffer;
	
};

class ShadowPass {
public:
	ShadowPass();
	~ShadowPass();

	void render(const std::shared_ptr<RenderScene>& scene);
	unsigned int get_UBO()const;
	std::vector<float> get_shadow_limiter() const;  // only 4 elements ,so we pass the value directly

public:
	std::shared_ptr<Shader> shadowShader_dir; 
	std::shared_ptr<Shader> shadowShader_point;
	std::vector<std::shared_ptr<FrameBuffer>> frameBuffer_dirs; 
	std::vector<std::shared_ptr<FrameBuffer>> frameBuffer_points; 
private:
	void pointLightShadow(const std::shared_ptr<RenderScene>& scene);
	void directionLightShadow(const std::shared_ptr<RenderScene>& scene);
	glm::mat4 get_stratified_matrix(const std::vector<glm::vec4>& points, const std::shared_ptr<DirectionLight>& light);
	std::vector<glm::vec4> get_frustum_points(const float nearplane, const float farplane, const std::shared_ptr<RenderScene>& scene);
	std::vector<glm::mat4> get_stratified_matrices(const std::shared_ptr<RenderScene>& scene, const std::shared_ptr<DirectionLight>);
	void init_framebuffers(const std::shared_ptr<RenderScene>& scene);
	unsigned int cube_map_resolution = 1024;
	unsigned int cascaded_map_resolution = 2048;
	unsigned int cascaded_layers = 5;
	
	float near_for_pointlight = 0.0;
	float far_for_pointlight = 0.0;  // the perspective parameter in generating shadow cube map
	bool dirty = 0;
	std::vector<float> shadow_limiter= { 0,0,0,0 };
	unsigned int  matrixUBO;


};

//TODO:
class DeferredPass {
public:
	DeferredPass();
	~DeferredPass();

	void renderGbuffer(const std::shared_ptr<RenderScene>& scene);
	void render(const std::shared_ptr<RenderScene>& scene);
	void postProcess(const std::shared_ptr<RenderScene>& scene);

public:
	shared_ptr<Shader> gBufferShader;
	shared_ptr<Shader> lightingShader;
	shared_ptr<Shader> postProcessShader;
	shared_ptr<FrameBuffer> gBuffer;
	shared_ptr<FrameBuffer> postBuffer;

	shared_ptr<RenderBuffer> rbo;
	shared_ptr<RenderBuffer> postRbo;

	shared_ptr<Texture> gPosition;
	shared_ptr<Texture> gNormal;
	shared_ptr<Texture> gAlbedoSpec;
	shared_ptr<Texture> gPBR;
	shared_ptr<Texture> postTexture;
	//shared_ptr<Texture> gPBR;

	unsigned int cascaded_matrix_UBO=0;  // we will pass this 2 values from the shadow pass 
	std::vector<float> shadow_limiter ={ 0,0,0,0 };
private:
	void initShader();
	void initTextures();
};


class RSMPass {
public:
	RSMPass();
	~RSMPass();

	void render(const std::shared_ptr<RenderScene>& scene);
public:
	std::shared_ptr<Shader> RSMShader;
	std::shared_ptr<FrameBuffer> rsmFBO; 
	std::shared_ptr<Texture> depthMap;
	std::shared_ptr<Texture> normalMap;
	std::shared_ptr<Texture> worldPosMap;
	std::shared_ptr<Texture> fluxMap;
	//std::shared_ptr<Texture> randomMap;
	bool dirty;
private:
	const GLuint RSM_WIDTH = 1024, RSM_HEIGHT = 1024;
	// const float light_near_plane = 0.5f, light_far_plane = 20.0f;
	void initShader();
	GLuint createRandomTexture(int size=64);
	void initTextures();
};