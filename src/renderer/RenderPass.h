#pragma once
#include<memory>

class RenderScene; 
class Shader;
class FrameBuffer;
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
/// base pass: renders scene objects, 
/// this is not a base class for other render pass classes
/// this class should not have any functions to operate on framebuffers
/// </summary>
class BasePass {
public:
	BasePass() = default; 
	~BasePass() = default;

	void render(const std::shared_ptr<RenderScene>& scene);
public:

};

class ShadowPass {
public:
	ShadowPass();
	~ShadowPass();

	void render(const std::shared_ptr<RenderScene>& scene);
public:
	std::shared_ptr<Shader> shadowShader; 
	std::shared_ptr<FrameBuffer> frameBuffer;
private:
	void pointLightShadow(const std::shared_ptr<RenderScene>& scene);
	void directionLightShadow(const std::shared_ptr<RenderScene>& scene);
};

//TODO:
class DeferredPass {

};
