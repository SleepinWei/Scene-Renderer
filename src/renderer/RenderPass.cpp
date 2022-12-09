#include<glad/glad.h>
#include<glfw/glfw3.h>
#include"./RenderPass.h"
#include"./RenderScene.h"
#include"../system/InputManager.h"
#include"../utils/Shader.h"
#include"../system/RenderManager.h"
#include"../utils/Utils.h"
#include"../renderer/Mesh_Renderer.h"
#include"../component/GameObject.h"
#include"../object/Terrain.h"
#include"../object/SkyBox.h"
#include"../component/Atmosphere.h"
#include"../component/TerrainComponent.h"
#include"../component/Grass.h"
#include"../buffer/FrameBuffer.h"
#include"../component/Lights.h"
#include"../renderer/Texture.h"
#include"../buffer/RenderBuffer.h"
#include<memory>

extern std::unique_ptr<InputManager> inputManager;
extern std::unique_ptr<RenderManager> renderManager;

void BasePass::render(const std::shared_ptr<RenderScene>& scene,const std::shared_ptr<Shader>& outShader) {
	glViewport(0, 0, inputManager->width, inputManager->height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

	glCheckError();
	for (auto object : scene->objects) {
		std::shared_ptr<MeshRenderer>&& renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer && renderer->shader) {
			renderer->render(outShader);
		}
	}
	glCheckError();
	// render Terrain 
	std::shared_ptr<Terrain>& terrain = scene->terrain;
	if (terrain) {
		//terrain->shader->use();
		glCheckError();
		terrain->render(outShader);
	}

	std::shared_ptr<Sky>& sky = scene->sky;
	if (sky) {
		sky->render(outShader);
	}
}

HDRPass::HDRPass() {
	hdrFBO = 0;
	colorBuffer = 0;
	rboDepth = 0;
	dirty = true;

	hdrShader = renderManager->getShader(ShaderType::HDR);
}

HDRPass::~HDRPass() {
	if (hdrFBO)
		glDeleteFramebuffers(1, &hdrFBO);
	if (colorBuffer)
		glDeleteTextures(1, &colorBuffer);
	if (rboDepth)
		glDeleteRenderbuffers(1, &rboDepth);
}

void HDRPass::initPass(int width, int height) {
	if(!hdrFBO)
		glGenFramebuffers(1, &hdrFBO);
	if (!rboDepth)
		glGenRenderbuffers(1, &rboDepth);
	if(!colorBuffer)
		glGenTextures(1, &colorBuffer);
	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//rbo
	glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
	//fbo
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepth);
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	// set hdrShader 
	hdrShader->setInt("hdrBuffer", 0);
}

void HDRPass::bindBuffer() {
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
}

void HDRPass::unbindBuffer() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void HDRPass::render() {
	// check whether Pass textures/buffers should be regenerated
	if (dirty || inputManager->viewPortChange) {
		initPass(inputManager->width, inputManager->height);
		dirty = false;
	}
	unbindBuffer();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	hdrShader->use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	renderQuad();
}

ShadowPass::ShadowPass() {
	// TODO: init shadowShader: get from renderManager

	// TODO: init framebuffer
	frameBuffer = std::make_shared<FrameBuffer>();
}

ShadowPass::~ShadowPass() {

}

void ShadowPass::render(const std::shared_ptr<RenderScene>& scene) {
	// TODO: 
	glViewport(0, 0, Light::SHADOW_WIDTH, Light::SHADOW_HEIGHT);
	frameBuffer->bindBuffer();

	// rendering shadow map

	frameBuffer->unbindBuffer();
}

void ShadowPass::pointLightShadow(const std::shared_ptr<RenderScene>& scene) {
	//TODO:
}

void ShadowPass::directionLightShadow(const std::shared_ptr<RenderScene>& scene) {
	//TODO:
	auto& dLights = scene->directionLights;
	for (auto& light : dLights) {
		if (!light || !light->castShadow) {
			continue;
		}
		frameBuffer->bindTexture(light->shadowTex, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		// 画所有的物体
	}
}

DepthPass::DepthPass() {
	// TODO :
	// shader
	depthShader = renderManager->getShader(ShaderType::DEPTH);
	depthShader->requireMat = false;

	//frame buffer
	frameBuffer = std::make_shared<FrameBuffer>();
	frameBuffer->bindBuffer();
	//glDrawBuffer(GL_NONE);
	//glReadBuffer(GL_NONE);

	frontDepth = std::make_shared<Texture>();
	backDepth = std::make_shared<Texture>();

	renderBuffer = std::make_shared<RenderBuffer>();

	dirty = true;
}

DepthPass::~DepthPass() {
	
}

void DepthPass::render(const std::shared_ptr<RenderScene>& scene) {
	frameBuffer->bindBuffer();
	if (dirty || inputManager->viewPortChange) {
		frontDepth->genTexture(GL_RGBA32F, GL_RGBA, inputManager->width, inputManager->height);
		backDepth->genTexture(GL_RGBA32F, GL_RGBA, inputManager->width, inputManager->height);
		renderBuffer->genBuffer(inputManager->width, inputManager->height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderBuffer->rbo);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
		dirty = false;
	}
	glCheckError();

	//glm::mat4 projection_ = glm::perspective(glm::radians(scene->main_camera->Zoom),
		//inputManager->width * 1.0f / inputManager->height,
		//0.5f, 5.0f);

	depthShader->use();
	//depthShader->setMat4("projection_", projection_);

	frameBuffer->bindTexture(frontDepth,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D);

	glViewport(0, 0, inputManager->width, inputManager->height);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);//black
	glCheckError();

	glCullFace(GL_BACK);
	for (auto object : scene->objects) {
		std::shared_ptr<MeshRenderer>&& renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer && renderer->shader) {
			renderer->render(depthShader);
		}
	}

	frameBuffer->bindTexture(backDepth,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);//black
	glCullFace(GL_FRONT);
	for (auto object : scene->objects) {
		std::shared_ptr<MeshRenderer>&& renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer && renderer->shader) {
			renderer->render(depthShader);
		}
	}

	// set shaders 
	auto& sssShader = renderManager->getShader(ShaderType::PBR_SSS);
	glActiveTexture(GL_TEXTURE18);
	glBindTexture(GL_TEXTURE_2D, frontDepth->id);
	glActiveTexture(GL_TEXTURE19);
	glBindTexture(GL_TEXTURE_2D, backDepth->id);

	glCullFace(GL_BACK);
	
	sssShader->use();
	sssShader->setInt("frontDepth", 18);
	sssShader->setInt("backDepth", 19);
	sssShader->setInt("screen_width", inputManager->width);
	sssShader->setInt("screen_height", inputManager->height);
}


DeferredPass::DeferredPass() {
	initShader();
	initTextures();
}

void DeferredPass::initShader() {
	gBufferShader= std::make_shared<Shader>("./src/shader/deferred/gBuffer.vs", "./src/shader/deferred/gBuffer.fs");
	gBufferShader->requireMat = true;
	lightingShader = std::make_shared<Shader>("./src/shader/deferred/deferred.vs", "./src/shader/deferred/deferred.fs");
	lightingShader->requireMat = true;
	postProcessShader = std::make_shared<Shader>("./src/shader/post/hdr.vs", "./src/shader/post/hdr.fs");
	postProcessShader->requireMat = false;
}

void DeferredPass::initTextures() {
	gBuffer = std::make_shared<FrameBuffer>();
	postBuffer = std::make_shared<FrameBuffer>();
	
	rbo = std::make_shared<RenderBuffer>();
	postRbo = std::make_shared<RenderBuffer>();

	gPosition = std::make_shared<Texture>();
	gNormal = std::make_shared<Texture>();
	gAlbedoSpec = std::make_shared<Texture>();
	gPBR = std::make_shared<Texture>();
	postTexture = std::make_shared<Texture>();
}

DeferredPass::~DeferredPass() {
	
}

void DeferredPass::renderGbuffer(const std::shared_ptr<RenderScene>& scene) {
	if (gBuffer->dirty || inputManager->viewPortChange) {
		gPosition->genTexture(GL_RGBA16F, GL_RGBA,inputManager->width,inputManager->height);
		gNormal->genTexture(GL_RGBA16F, GL_RGBA, inputManager->width, inputManager->height);
		gAlbedoSpec->genTexture(GL_RGBA16F, GL_RGBA, inputManager->width, inputManager->height);
		gPBR->genTexture(GL_RGBA16F, GL_RGBA, inputManager->width, inputManager->height);
		postTexture->genTexture(GL_RGBA16F, GL_RGBA, inputManager->width, inputManager->height);
		
		rbo->genBuffer(inputManager->width, inputManager->height);
		postRbo->genBuffer(inputManager->width, inputManager->height);
	}

	if (gBuffer->dirty) {
		// set attachments
		gBuffer->dirty = false;

		gBuffer->bindTexture(gPosition, GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D);
		gBuffer->bindTexture(gNormal, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D);
		gBuffer->bindTexture(gAlbedoSpec, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D);
		gBuffer->bindTexture(gPBR, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D);

		gBuffer->bindBuffer();
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo->rbo);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete!" << std::endl;
		// finally check if framebuffer is complete
		unsigned int attachments[] = { 
			GL_COLOR_ATTACHMENT0, 
			GL_COLOR_ATTACHMENT1, 
			GL_COLOR_ATTACHMENT2,
			GL_COLOR_ATTACHMENT3
		};
		glDrawBuffers(4, attachments);

		// post buffer
		postBuffer->bindTexture(postTexture, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);
		postBuffer->bindBuffer();
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, postRbo->rbo);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete!" << std::endl;
		// finally check if framebuffer is complete
	}

	gBuffer->bindBuffer();
	glViewport(0, 0, inputManager->width, inputManager->height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	gBufferShader->use();

	for (int i = 0; i < scene->objects.size();i++) {
		auto& object = scene->objects[i];
		// only render deferred objects in gbuffer phase
		if (object->isDeferred()) {
			std::shared_ptr<MeshRenderer>&& renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
			if (renderer && renderer->shader) {
				renderer->render(gBufferShader);
			}
		}
	}

	std::shared_ptr<Terrain>& terrain = scene->terrain;
	if (terrain) {
		//terrain->shader->use();
		glCheckError();
		//auto& terrainComponent = std::static_pointer_cast<TerrainComponent>(terrain->GetComponent("TerrainComponent"));
		//if (terrainComponent) {
			//terrainComponent->render(terrainComponent->terrainGBuffer);
		//}
		terrain->render(nullptr);
	}
	// no sky
}

void DeferredPass::render(const std::shared_ptr<RenderScene>& scene) {
	// renderScene
	//glBindFramebuffer(GL_FRAMEBUFFER, 0);
	postBuffer->bindBuffer();
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

	lightingShader->use();
	// bind textures
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gPosition->id);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gNormal->id);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, gAlbedoSpec->id);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, gPBR->id);

	lightingShader->setInt("gPosition", 0);
	lightingShader->setInt("gNormal", 1);
	lightingShader->setInt("gAlbedoSpec", 2);
	lightingShader->setInt("gPBR", 3);

	// set uniforms
	if (scene->main_camera) {
		lightingShader->setVec3("camPos", scene->main_camera->Position);
	}

	// render quad
	renderQuad();

	// copy renderbuffer
	glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer->FBO);
	//glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postBuffer->FBO);
	int width = inputManager->width;
	int height = inputManager->height;
	glBlitFramebuffer(0, 0, width, height,
		0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	postBuffer->bindBuffer();

	// forward rendering 
	for (auto& object : scene->objects) {
		if (!object->isDeferred()) {
			std::shared_ptr<MeshRenderer>&& renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
			if (renderer && renderer->shader) {
				renderer->render(renderer->shader);
			}
		}
	}

	// forward rendering : sky
	if (scene->sky) {
		scene->sky->render(nullptr);
	}
}

void DeferredPass::postProcess(const std::shared_ptr<RenderScene>& scene) {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, postTexture->id);

	postProcessShader->use();
	postProcessShader->setFloat("exposure", scene->main_camera->exposure);
	postProcessShader->setInt("hdrBuffer", 0);

	renderQuad();
}
