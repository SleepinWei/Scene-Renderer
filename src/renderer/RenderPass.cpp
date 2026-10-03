#include <glad/glad.h>
#include "renderer/RenderPass.h"
#include "renderer/TemporalAA.h"
#include "buffer/FrameBuffer.h"
#include "buffer/RenderBuffer.h"
#include "buffer/ImageTexture.h"
#include "buffer/UniformBuffer.h"
#include "component/Atmosphere.h"
#include "component/GameObject.h"
#include "component/Grass.h"
#include "component/Lights.h"
#include "component/Mesh_Renderer.h"
#include "component/Ocean.h"
#include "component/TerrainComponent.h"
#include "component/transform.h"
#include "object/SkyBox.h"
#include "object/Terrain.h"
#include "renderer/RenderScene.h"
#include "renderer/Texture.h"
#include "system/InputManager.h"
#include "system/RenderManager.h"
#include "utils/Shader.h"
#include "utils/Utils.h"
#include <glfw/glfw3.h>
#include <memory>
#include <random>
#include<glm/gtc/type_ptr.hpp>

void BasePass::render(const std::shared_ptr<RenderScene> &scene, const std::shared_ptr<Shader> &outShader)
{
	glViewport(0, 0, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	glCheckError();
	for (auto &object : scene->objects())
	{
		std::shared_ptr<MeshRenderer> &&renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer && renderer->getShader())
		{
			renderer->render(outShader);
		}
	}
	glCheckError();

	// render Terrain
	const auto &terrain = scene->terrain();
	if (terrain)
	{
		// terrain->shader->use();

		terrain->render(outShader);
		glCheckError();
	}

	const auto &sky = scene->sky();
	if (sky)
	{
		sky->render(outShader);
		glCheckError();
	}
}

PostPass::PostPass()
{
	hdrFBO = 0;
	colorBuffer = 0;
	rboDepth = 0;
	dirty = true;

	postShader = RenderManager::GetInstance()->getShader(ShaderType::HDR);
}

PostPass::~PostPass()
{
	if (hdrFBO)
		glDeleteFramebuffers(1, &hdrFBO);
	if (colorBuffer)
		glDeleteTextures(1, &colorBuffer);
	if (rboDepth)
		glDeleteRenderbuffers(1, &rboDepth);
}

void PostPass::initPass(int width, int height)
{
	if (!hdrFBO)
		glGenFramebuffers(1, &hdrFBO);
	if (!rboDepth)
		glGenRenderbuffers(1, &rboDepth);
	if (!colorBuffer)
		glGenTextures(1, &colorBuffer);
	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// rbo
	glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
	// fbo
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepth);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// set hdrShader
	postShader->setInt("hdrBuffer", 0);
}

void PostPass::bindBuffer()
{
	if (dirty || InputManager::GetInstance()->viewPortChange)
	{
		initPass(InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		dirty = false;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
}

void PostPass::unbindBuffer()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostPass::render()
{
	unbindBuffer();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	postShader->use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	renderQuad();
}

ShadowPass::ShadowPass()
{
	// TODO: init shadowShader: get from renderManager
	shadowShader_dir = std::make_shared<Shader>("./src/shader/shadow/cascaded_shadow_depth.vs", "./src/shader/shadow/cascaded_shadow_depth.fs", "./src/shader/shadow/cascaded_shadow_depth.gs");
	// shadowShader_dir = std::make_shared<Shader>("./src/shader/shadow/direction.vs", "./src/shader/shadow/direction.fs");
	shadowShader_dir->requireMat = false;
	shadowShader_point = std::make_shared<Shader>("./src/shader/shadow/point_shadow_depth.vs", "./src/shader/shadow/point_shadow_depth.fs", "./src/shader/shadow/point_shadow_depth.gs");
	shadowShader_point->requireMat = false;

	// TODO: init framebuffer
	// for creating multiple depth attachment for a fb is not allowed

	// set UBO
	matrixBuffer = std::make_shared<UniformBuffer>(sizeof(glm::mat4) * 10 * cascaded_layers);
	// suppose 5 is the cascaded levels, 10 is the max num of directional lights
	//  this way is kinda undecent ,but convenient.

}

ShadowPass::~ShadowPass() = default;

void ShadowPass::render(const std::shared_ptr<RenderScene> &scene)
{
	// TODO:

	// rendering shadow map
	pointLightShadow(scene);
	directionLightShadow(scene); // after evoke these 2 functions, the depth maps are restored in the lights' texes.
}

void ShadowPass::pointLightShadow(const std::shared_ptr<RenderScene> &scene)
{
	// TODO:
	auto &plights = scene->pointLights();
	unsigned int num_point_lights = plights.size();
	for (unsigned int i = 0; i < num_point_lights; i++)
	{
		const auto &light = plights.at(i);
		if (!light || !light->castsShadow())
		{
			continue;
		}
		if (dirty)
			init_framebuffers(scene);

		shadowShader_point->use();
		const auto &current_framebuffer = frameBuffer_points.at(i);

		current_framebuffer->bindBuffer();
		// glEnable(GL_DEPTH_TEST);
		// glEnable(GL_CULL_FACE);
		//  glDisable(GL_CULL_FACE);

		// current_framebuffer->bindShadowTexture(light->shadowTex, GL_DEPTH_ATTACHMENT);
		// glDrawBuffer(GL_NONE);
		// glReadBuffer(GL_NONE);
		//
		//  start to render
		float near_plane = scene->mainCamera()->getNear();
		float far_plane = scene->mainCamera()->getFar();

		glm::mat4 proj = glm::perspective(glm::radians(90.0f), (float)cube_map_resolution / cube_map_resolution, near_plane, far_plane);

		std::vector<glm::mat4> omnishadow_matrices;
		for (unsigned i = 0; i < 6; i++)
		{
			auto &&transforms = light->getLightTransform(i);
			omnishadow_matrices.emplace_back(std::get<0>(transforms) * std::get<1>(transforms));
		}

		glViewport(0, 0, cube_map_resolution, cube_map_resolution);
		glClear(GL_DEPTH_BUFFER_BIT);

		for (unsigned i = 0; i < 6; i++)
			shadowShader_point->setMat4("shadowMatrices[" + std::to_string(i) + "]", omnishadow_matrices[i]);

		/// <summary>
		/// PENDING!!!
		/// </summary>
		/// <param name="scene"></param>
		glm::vec3 lightPos = glm::vec3(0.0, 0.0, 0.0);
		auto trans = std::static_pointer_cast<Transform>(light->owner()->GetComponent("Transform"));
		lightPos = trans->getPosition();
		shadowShader_point->setFloat("far_plane", far_plane);
		shadowShader_point->setVec3("lightPos", lightPos);

		for (auto &object : scene->objects())
		{
			std::shared_ptr<MeshRenderer> &&renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
			if (renderer && renderer->getDrawMode() == GL_TRIANGLES)
			{ // due to geometry shader, drawing points is not allowed;
				renderer->render(shadowShader_point);
			}
		}
	}
}

void ShadowPass::simpleDirectionShadow(const std::shared_ptr<RenderScene> &scene)
{
	// pass
}

void ShadowPass::directionLightShadow(const std::shared_ptr<RenderScene> &scene)
{
	// TODO:
	auto &dLights = scene->directionLights();
	unsigned int num_direction_lights = dLights.size();
	if (!RenderManager::GetInstance()->setting.enableDirectional)
	{
		num_direction_lights = 0;
	}
	for (unsigned int i = 0; i < num_direction_lights; i++)
	{
		const auto &light = dLights.at(i);
		if (!light || !light->castsShadow())
		{
			continue;
		}
		if (dirty)
			init_framebuffers(scene);

		const auto &current_framebuffer = frameBuffer_dirs.at(i);
		current_framebuffer->bindBuffer();
		// glEnable(GL_DEPTH_TEST);

		shadowShader_dir->use();
		// current_framebuffer->bindShadowTexture(light->shadowTex, GL_DEPTH_ATTACHMENT);
		// glDrawBuffer(GL_NONE);
		// glReadBuffer(GL_NONE);

		// send the matrices into the uniform variable in shaders
		std::vector<glm::mat4> light_matrices = get_stratified_matrices(scene, light);

		// binding an UBO
		for (unsigned j = 0; j < light_matrices.size(); j++)
		{
			// i-th light j-th level
			matrixBuffer->write((i * cascaded_layers + j) * sizeof(glm::mat4), sizeof(glm::mat4), &light_matrices[j]);
		}
		/*******/

		for (unsigned i = 0; i < light_matrices.size(); i++)
		{
			shadowShader_dir->setMat4("lightSpaceMatrices[" + std::to_string(i) + "]", light_matrices.at(i));
		}

		glViewport(0, 0, this->cascaded_map_resolution, this->cascaded_map_resolution);
		glClear(GL_DEPTH_BUFFER_BIT);
		// glCullFace(GL_FRONT);

		// rendering

		for (auto object : scene->objects())
		{
			std::shared_ptr<MeshRenderer> &&renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
			if (renderer && renderer->getDrawMode() == GL_TRIANGLES)
			{ // due to implemetation of geometry shader, drawing points is not allowed
				renderer->render(shadowShader_dir);
			}
		}

		// glCullFace(GL_BACK);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}

std::vector<glm::vec4> ShadowPass::get_frustum_points(const float nearplane, const float farplane, const std::shared_ptr<RenderScene> &scene)
{
	std::vector<glm::vec4> re;
	const auto &camera = scene->mainCamera();
	glm::mat4 perspective = glm::perspective(glm::radians(camera->getZoom()), camera->getAspect(), nearplane, farplane);
	glm::mat4 view = camera->GetViewMatrix();

	glm::mat4 inv = glm::inverse(perspective * view);
	for (unsigned z = 0; z < 2; z++)
		for (unsigned y = 0; y < 2; y++)
			for (unsigned x = 0; x < 2; x++)
			{
				glm::vec4 tmp = inv * glm::vec4(2.0 * x - 1.0, 2.0 * y - 1.0, 2.0 * z - 1.0, 1.0);
				re.emplace_back(tmp / tmp.w);
			}
	return re;
}

glm::mat4 ShadowPass::get_stratified_matrix(const std::vector<glm::vec4> &points, const std::shared_ptr<DirectionLight> &light)
{
	std::vector<glm::mat4> re;
	glm::vec3 center = glm::vec3(0.0, 0.0, 0.0);
	for (const auto &i : points)
	{
		center += glm::vec3(i);
	}

	center /= points.size(); // the center of the frustum in world space
	// TODO: make the direction light outside of the house.
	auto light_view = glm::lookAt(center - 1.0f * light->getData().direction, center, glm::vec3(0.0, 1.0, 0.0));

	float minX = std::numeric_limits<float>::max();
	float maxX = std::numeric_limits<float>::min();
	float minY = std::numeric_limits<float>::max();
	float maxY = std::numeric_limits<float>::min();
	float minZ = std::numeric_limits<float>::max();
	float maxZ = std::numeric_limits<float>::min();

	for (const auto &i : points)
	{
		auto frustm_point_light_space = light_view * i;
		minX = frustm_point_light_space.x < minX ? frustm_point_light_space.x : minX;
		maxX = frustm_point_light_space.x > maxX ? frustm_point_light_space.x : maxX;
		minY = frustm_point_light_space.y < minY ? frustm_point_light_space.y : minY;
		maxY = frustm_point_light_space.y > maxY ? frustm_point_light_space.y : maxY;
		minZ = frustm_point_light_space.z < minZ ? frustm_point_light_space.z : minZ;
		maxZ = frustm_point_light_space.z > maxZ ? frustm_point_light_space.z : maxZ;
	}

	// EXPAND z
	float z_ratio = 10.0;
	minZ = minZ < 0 ? 1.0 * minZ * z_ratio : 1.0 * minZ / z_ratio;
	maxZ = maxZ < 0 ? 1.0 * maxZ / z_ratio : 1.0 * maxZ * z_ratio;

	const glm::mat4 light_proj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

	return light_proj * light_view;
}

std::vector<glm::mat4> ShadowPass::get_stratified_matrices(const std::shared_ptr<RenderScene> &scene, const std::shared_ptr<DirectionLight> light)
{
	std::vector<glm::mat4> re;
	float camera_near = scene->mainCamera()->getNear(), camera_far = scene->mainCamera()->getFar();
	shadow_limiter.at(0) = camera_far / 50.0;
	shadow_limiter.at(1) = camera_far / 25.0;
	shadow_limiter.at(2) = camera_far / 10.0;
	shadow_limiter.at(3) = camera_far / 2.0;

	float near, far;
	for (unsigned i = 0; i < 5; i++)
	{
		near = i == 0 ? camera_near : shadow_limiter.at(i - 1);
		far = i == 4 ? camera_far : shadow_limiter.at(i);
		std::vector<glm::vec4> frustum_points = get_frustum_points(near, far, scene);
		glm::mat4 light_matrix = get_stratified_matrix(frustum_points, light);
		re.emplace_back(light_matrix);
	}
	return re;
}

void ShadowPass::init_framebuffers(const std::shared_ptr<RenderScene> &scene)
{
	unsigned int num_direction_lights = scene->directionLights().size();
	unsigned int num_point_lights = scene->pointLights().size();

	for (const auto &light : scene->directionLights())
	{
		light->shadowTex->genTextureArray(
			GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, cascaded_map_resolution, cascaded_map_resolution, 0, cascaded_layers);

		auto new_framebuffer = std::make_shared<FrameBuffer>();
		new_framebuffer->bindBuffer();
		new_framebuffer->bindShadowTexture(light->shadowTex, GL_DEPTH_ATTACHMENT);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		// add to vectors
		frameBuffer_dirs.emplace_back(new_framebuffer);
	}
	for (const auto &light : scene->pointLights())
	{
		light->shadowTex->genCubeMap(GL_DEPTH_COMPONENT, cube_map_resolution, cube_map_resolution);
		auto new_framebuffer = std::make_shared<FrameBuffer>();
		new_framebuffer->bindBuffer();
		new_framebuffer->bindShadowTexture(light->shadowTex, GL_DEPTH_ATTACHMENT);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		// add to vectors
		frameBuffer_points.emplace_back(new_framebuffer);
	}

	dirty = false;
	// once we generate these framebuffers, we set dirty as true to avoid repeatedly do these procedures in every pass
}

std::shared_ptr<UniformBuffer> ShadowPass::getMatrixBuffer() const
{
	return matrixBuffer;
}

std::vector<float> ShadowPass::get_shadow_limiter() const
{
	return this->shadow_limiter;
}

DepthPass::DepthPass()
{
	// TODO :
	// shader
	depthShader = RenderManager::GetInstance()->getShader(ShaderType::DEPTH);
	depthShader->requireMat = false;

	// frame buffer
	frameBuffer = std::make_shared<FrameBuffer>();
	frameBuffer->bindBuffer();
	// glDrawBuffer(GL_NONE);
	// glReadBuffer(GL_NONE);

	frontDepth = std::make_shared<Texture>();
	backDepth = std::make_shared<Texture>();

	renderBuffer = std::make_shared<RenderBuffer>();

	dirty = true;
}

DepthPass::~DepthPass()
{
}

void DepthPass::render(const std::shared_ptr<RenderScene> &scene)
{
	frameBuffer->bindBuffer();
	if (dirty || InputManager::GetInstance()->viewPortChange)
	{
		frontDepth->genTexture(GL_RGBA32F, GL_RGBA, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		backDepth->genTexture(GL_RGBA32F, GL_RGBA, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		renderBuffer->genBuffer(InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderBuffer->rbo);
		frameBuffer->bindTexture(frontDepth, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
		dirty = false;
	}

	// glm::mat4 projection_ = glm::perspective(glm::radians(scene->mainCamera()->getZoom()),
	// InputManager::GetInstance()->width * 1.0f / InputManager::GetInstance()->height,
	// 0.5f, 5.0f);

	depthShader->use();
	// depthShader->setMat4("projection_", projection_);

	frameBuffer->bindTexture(frontDepth, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);

	glViewport(0, 0, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // black

	glCullFace(GL_BACK);
	for (auto &object : scene->objects())
	{
		std::shared_ptr<MeshRenderer> &&renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer && renderer->getShader())
		{
			renderer->render(depthShader);
		}
	}

	frameBuffer->bindTexture(backDepth, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // black
	glCullFace(GL_FRONT);
	for (auto &object : scene->objects())
	{
		std::shared_ptr<MeshRenderer> &&renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer && renderer->getShader())
		{
			renderer->render(depthShader);
		}
	}

	// set shaders
	auto &&sssShader = RenderManager::GetInstance()->getShader(ShaderType::PBR_SSS);
	glActiveTexture(GL_TEXTURE18);
	glBindTexture(GL_TEXTURE_2D, frontDepth->gpuId());
	glActiveTexture(GL_TEXTURE19);
	glBindTexture(GL_TEXTURE_2D, backDepth->gpuId());

	glCullFace(GL_BACK);

	sssShader->use();
	sssShader->setInt("frontDepth", 18);
	sssShader->setInt("backDepth", 19);
	sssShader->setInt("screen_width", InputManager::GetInstance()->width);
	sssShader->setInt("screen_height", InputManager::GetInstance()->height);
}

float lerp(float a, float b, float f)
{
	return a + f * (b - a);
}


DeferredPass::DeferredPass()
{
	initShader();
	initTextures();
}

void DeferredPass::initShader()
{
	gBufferShader = std::make_shared<Shader>("./src/shader/deferred/gBuffer.vs", "./src/shader/deferred/gBuffer.fs");
	gBufferShader->requireMat = true;
	lightingShader = std::make_shared<Shader>("./src/shader/deferred/deferred.vs", "./src/shader/deferred/deferred.fs");
	lightingShader->requireMat = true;
	postProcessShader = std::make_shared<Shader>("./src/shader/post/hdr.vs", "./src/shader/post/hdr.fs");
	postProcessShader->requireMat = false;
}

void DeferredPass::initTextures()
{
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

DeferredPass::~DeferredPass()
{
}

void DeferredPass::renderGbuffer(const std::shared_ptr<RenderScene> &scene)
{
	if (gBuffer->dirty || InputManager::GetInstance()->viewPortChange)
	{
		gPosition->genTexture(GL_RGBA16F, GL_RGBA, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		gNormal->genTexture(GL_RGBA16F, GL_RGBA, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		gAlbedoSpec->genTexture(GL_RGBA16F, GL_RGBA, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		gPBR->genTexture(GL_RGBA16F, GL_RGBA, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		postTexture->genTexture(GL_RGBA16F, GL_RGBA, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		rbo->genBuffer(InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		postRbo->genBuffer(InputManager::GetInstance()->width, InputManager::GetInstance()->height);
	}

	if (gBuffer->dirty)
	{
		// set attachments
		gBuffer->dirty = false;

		gBuffer->bindTexture(gPosition, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);
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
			GL_COLOR_ATTACHMENT3};
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
	glViewport(0, 0, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	gBufferShader->use();

	for (int i = 0; i < scene->objects().size(); i++)
	{
		auto &object = scene->objects()[i];
		// only render deferred objects in gbuffer phase
		if (object->isDeferred())
		{
			std::shared_ptr<MeshRenderer> &&renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
			if (renderer && renderer->getShader())
			{
				renderer->render(gBufferShader);
			}
		}
	}

	const auto &terrain = scene->terrain();
	if (terrain)
	{
		// terrain->shader->use();

		// auto& terrainComponent = std::static_pointer_cast<TerrainComponent>(terrain->GetComponent("TerrainComponent"));
		// if (terrainComponent) {
		// terrainComponent->render(terrainComponent->terrainGBuffer);
		//}
		terrain->render(nullptr);
	}
	// no sky
}

void DeferredPass::render(const std::shared_ptr<RenderScene> &scene)
{
	// renderScene
	// bindings
	if (cascadedMatrixBuffer)
	{
		cascadedMatrixBuffer->setBinding(5);
	}

	glCheckError();
	postBuffer->bindTexture(postTexture, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);
	postBuffer->bindBuffer();
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	lightingShader->use();
	// bind textures
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gNormal->gpuId());
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, gAlbedoSpec->gpuId());
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, gPBR->gpuId());
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, gPosition->gpuId());
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (scene->sky()) {
		auto atmosphere = std::static_pointer_cast<Atmosphere>(scene->sky()->GetComponent("Atmosphere"));
		if (atmosphere && atmosphere->convolutionTexture) {
			atmosphere->convolutionTexture->bindBuffer(); // bind environment map
		}
		glActiveTexture(GL_TEXTURE6);
		if (atmosphere && atmosphere->skyViewTexture) {
			atmosphere->skyViewTexture->bindBuffer();
		}
	}
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D,RenderManager::GetInstance()->ssaoPass->gSSAO->gpuId());

	glCheckError();

	// int i = 0;
	int base = 8; // already 8 texture units occupied
	for (int i = 0; i < scene->directionLights().size(); ++i)
	{
		int texture_unit_index = i + base;
		glActiveTexture(GL_TEXTURE0 + texture_unit_index);
		glBindTexture(GL_TEXTURE_2D_ARRAY, scene->directionLights().at(i)->shadowTex->gpuId());
		lightingShader->setInt("shadow_maps[" + std::to_string(i) + "]", texture_unit_index);
	}

	base += scene->directionLights().size();
	for (int i = 0; i < scene->pointLights().size(); ++i)
	{
		int texture_unit_index = i + base;
		glActiveTexture(GL_TEXTURE0 + texture_unit_index);
		glBindTexture(GL_TEXTURE_CUBE_MAP, scene->pointLights().at(i)->shadowTex->gpuId());
		lightingShader->setInt("shadow_cubes[" + std::to_string(i) + "]", texture_unit_index);
	}

	lightingShader->setInt("gPosition", 4);
	lightingShader->setInt("gNormal", 1);
	lightingShader->setInt("gAlbedoSpec", 2);
	lightingShader->setInt("gPBR", 3);

	lightingShader->setBool("enableShadow", RenderManager::GetInstance()->setting.enableShadow);

	lightingShader->setFloat("far_plane", scene->mainCamera()->getFar());
	lightingShader->setInt("cascaded_levels", 4);

	lightingShader->setInt("environment", 5);
	lightingShader->setInt("specular_map", 6);
	lightingShader->setInt("gSSAO", 7);

	lightingShader->setInt("enbale_ssao", RenderManager::GetInstance()->setting.enableSSAO);
	lightingShader->setFloat("SCR_WIDTH", InputManager::GetInstance()->width);
	lightingShader->setFloat("SCR_HEIGHT", InputManager::GetInstance()->height);
	glCheckError();

	for (unsigned int i = 0; i < 4; i++)
		lightingShader->setFloat("cascaded_distances[" + std::to_string(i) + "]", shadow_limiter[i]);

	// set uniforms
	if (scene->mainCamera())
	{
		// lightingShader->setVec3("camPos", scene->mainCamera()->getPosition());

		lightingShader->setFloat("far_plane", scene->mainCamera()->getFar());
		// lightingShader->setInt("cascaded_levels", 5);
	}

	// render quad
	renderQuad();
	glCheckError();

	// copy renderbuffer
	glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer->FBO);
	// glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postBuffer->FBO);
	int width = InputManager::GetInstance()->width;
	int height = InputManager::GetInstance()->height;
	glBlitFramebuffer(0, 0, width, height,
					  0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	// glBindFramebuffer(GL_FRAMEBUFFER, 0);
	postBuffer->bindTexture(postTexture, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);
	postBuffer->bindBuffer();

	// forward rendering
	for (auto &object : scene->objects())
	{
		if (!object->isDeferred())
		{
			std::shared_ptr<MeshRenderer> &&renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
			if (renderer && renderer->getShader())
			{
				renderer->render(renderer->getShader());
			}
		}
	}

	glCheckError();

	// forward rendering : sky
	if (scene->sky())
	{
		scene->sky()->render(nullptr);
	}

	glCheckError();
}

void DeferredPass::renderAlphaObjects(const std::shared_ptr<RenderScene> &scene)
{
	// 缁戝畾FrameBuffer
	if (RenderManager::GetInstance()->setting.enableRSM)
		postBuffer->bindTexture(RenderManager::GetInstance()->rsmPass->outTexture, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);
	postBuffer->bindBuffer();

	// postTexture = alphaTexture;
	if (scene->terrain() && scene->terrain()->GetComponent("Ocean") != nullptr)
	{
		shared_ptr<Ocean> ocean = std::static_pointer_cast<Ocean>((scene->terrain()->GetComponent("Ocean")));
		shared_ptr<Shader> oceanShader = ocean->draw_shader;
		std::static_pointer_cast<Ocean>((scene->terrain()->GetComponent("Ocean")))->render();
	}
}

void DeferredPass::postProcess(const std::shared_ptr<RenderScene> &scene)
{
    auto manager=RenderManager::GetInstance();
    unsigned source=manager->setting.enableRSM?manager->rsmPass->outTexture->gpuId():postTexture->gpuId();
    if(manager->temporalAA)source=manager->temporalAA->resolve(source,postBuffer->FBO);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);
	postProcessShader->use();
	postProcessShader->setFloat("exposure", scene->mainCamera()->getExposure());
	postProcessShader->setInt("hdrBuffer", 0);

	renderQuad();
}

RSMPass::RSMPass()
{
	initShader();
	initTextures();
}

RSMPass::~RSMPass()
{
}
void RSMPass::initShader()
{
	RSMShader = std::make_shared<Shader>("./src/shader/rsm/lightSpace.vs", "./src/shader/rsm/lightSpace.fs");
	RSMShader->requireMat = true;
	indirectShader = std::make_shared<Shader>("./src/shader/rsm/rsm.vs", "./src/shader/rsm/rsm.fs");
	indirectShader->requireMat = false;
	// RSMShader->use();
}

GLuint RSMPass::createRandomTexture(int size)
{
    // Prefix-stable R2 sequence: every sampleCount uses the entire disk.
    std::vector<glm::vec3> samples(size);
    const float pi = std::acos(-1.0f);
    for (int i = 0; i < size; ++i) {
        float u = std::fmod(0.5 + (i + 1) * 0.7548776662466927, 1.0);
        float v = std::fmod(0.5 + (i + 1) * 0.5698402909980532, 1.0);
        float radius = std::sqrt(u); // Uniform area density, NOT uniform radius.
        samples[i] = {radius * std::cos(2 * pi * v), radius * std::sin(2 * pi * v), 1};
    }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, size, 1, 0, GL_RGB, GL_FLOAT, samples.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return texture;
}
void RSMPass::initTextures()
{
	rsmFBO = std::make_shared<FrameBuffer>();
	rsmBuffer = std::make_shared<FrameBuffer>();

	depthMap = std::make_shared<Texture>();
	normalMap = std::make_shared<Texture>();
	worldPosMap = std::make_shared<Texture>();
	fluxMap = std::make_shared<Texture>();
	outTexture = std::make_shared<Texture>();
	rbo = std::make_shared<RenderBuffer>();
	randomMap = createRandomTexture();
	depthMap->genTexture(GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, RSM_WIDTH, RSM_HEIGHT);
	normalMap->genTexture(GL_RGBA32F, GL_RGBA, RSM_WIDTH, RSM_HEIGHT);
	worldPosMap->genTexture(GL_RGBA32F, GL_RGBA, RSM_WIDTH, RSM_HEIGHT);
	fluxMap->genTexture(GL_RGBA32F, GL_RGBA, RSM_WIDTH, RSM_HEIGHT);
    for (const auto& texture : {normalMap, worldPosMap, fluxMap}) {
        glBindTexture(GL_TEXTURE_2D, texture->gpuId());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}

void RSMPass::renderGbuffer(const std::shared_ptr<RenderScene> &scene)
{
	if (rsmFBO->dirty)
	{
		// set attachments
		rsmFBO->dirty = false;

		rsmFBO->bindTexture(depthMap, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D);
		rsmFBO->bindTexture(normalMap, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);
		rsmFBO->bindTexture(worldPosMap, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D);
		rsmFBO->bindTexture(fluxMap, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D);

		rsmFBO->bindBuffer();
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete!" << std::endl;
		// finally check if framebuffer is complete
		unsigned int attachments[] = {
			GL_COLOR_ATTACHMENT0,
			GL_COLOR_ATTACHMENT1,
			GL_COLOR_ATTACHMENT2};
		glDrawBuffers(3, attachments);

	}

    // Generation and gather share one cached projection. Outdoor scenes use
    // directional irradiance and the same diffuse sky LUT as deferred IBL.
    auto atmosphere = scene->sky() ? std::static_pointer_cast<Atmosphere>(scene->sky()->GetComponent("Atmosphere")) : nullptr;
    std::shared_ptr<DirectionLight> sun;
    for (const auto& candidate : scene->directionLights())
        if (candidate && candidate->isEnabled()) { sun = candidate; break; }
    const bool outdoor = useSunSky && (sun || atmosphere);
    light.reset();
    if (!outdoor) for (const auto& candidate : scene->spotLights())
        if (candidate && candidate->isEnabled()) { light = candidate; break; }
    sourceAvailable = outdoor || bool(light);
    rsmFBO->bindBuffer();
    glViewport(0, 0, RSM_WIDTH, RSM_HEIGHT);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!sourceAvailable) return;
    RSMShader->use();
    if (outdoor) {
        const auto direction = sun ? glm::normalize(sun->getData().direction) : glm::vec3(0,-1,0);
        const auto up = std::abs(direction.y) > .99f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
        const float radius = std::max(worldRadius, 1.0f);
        glm::vec3 center = scene->mainCamera()->getPosition() + scene->mainCamera()->getFront() * (radius * .5f);
        const auto right = glm::normalize(glm::cross(direction,up));
        const auto lightUp = glm::cross(right,direction);
        const float texelSize = 2 * radius / RSM_WIDTH;
        center -= right * std::fmod(glm::dot(center,right),texelSize) + lightUp * std::fmod(glm::dot(center,lightUp),texelSize);
        lightSpaceMatrix = glm::ortho(-radius,radius,-radius,radius,.1f,4*radius) *
                           glm::lookAt(center-direction*(2*radius),center,up);
        RSMShader->setInt("light.type",1);
        RSMShader->setVec3("light.Direction",direction);
        RSMShader->setVec3("light.Color",sun && sunBounce && RenderManager::GetInstance()->setting.enableDirectional ? sun->getData().color : glm::vec3(0));
        const bool sky = atmosphere && skyBounce;
        RSMShader->setInt("enableSky",sky?1:0);
        glActiveTexture(GL_TEXTURE19);
        glBindTexture(GL_TEXTURE_2D,sky ? atmosphere->convolutionTexture->tex->gpuId() : 0);
        RSMShader->setInt("skyIrradiance",19);
    } else {
        auto trans = std::static_pointer_cast<Transform>(light->owner()->GetComponent("Transform"));
        const auto direction = glm::normalize(light->getData().direction);
        const auto up = std::abs(direction.y) > .99f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
        lightSpaceMatrix = glm::perspective(2*std::acos(glm::clamp(light->getData().outerCutOff,-.999f,.999f)),
                                          float(RSM_WIDTH)/RSM_HEIGHT,light->getNear(),light->getFar()) *
                           glm::lookAt(trans->getPosition(),trans->getPosition()+direction,up);
        RSMShader->setInt("light.type",0);
        RSMShader->setVec3("light.Position",trans->getPosition());
        RSMShader->setVec3("light.Color",light->getData().color);
        RSMShader->setVec3("light.Direction",direction);
        RSMShader->setFloat("light.cutOff",light->getData().cutOff);
        RSMShader->setFloat("light.outerCutOff",light->getData().outerCutOff);
        RSMShader->setInt("enableSky",0);
    }
    RSMShader->setMat4("lightSpaceMatrix",lightSpaceMatrix);

	for (int i = 0; i < scene->objects().size(); i++)
	{
		auto &object = scene->objects()[i];
		//// only render deferred objects in gbuffer phase
		// if (object->isDeferred()) {
		std::shared_ptr<MeshRenderer> &&renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer && renderer->getShader())
		{
			renderer->render(RSMShader);
		}
		//}
	}

	// std::shared_ptr<Terrain>& terrain = scene->terrain();
	// if (terrain) {
	//	//terrain->shader->use();
	//
	//	//auto& terrainComponent = std::static_pointer_cast<TerrainComponent>(terrain->GetComponent("TerrainComponent"));
	//	//if (terrainComponent) {
	//		//terrainComponent->render(terrainComponent->terrainGBuffer);
	//	//}
	//	terrain->render(nullptr);
	// }
	//  no sky
}

void RSMPass::render(const std::shared_ptr<RenderScene>& scene)
{
    const int width = InputManager::GetInstance()->width;
    const int height = InputManager::GetInstance()->height;
    if (rsmBuffer->dirty || outTexture->getWidth() != width || outTexture->getHeight() != height) {
        rsmBuffer->dirty = false;
        outTexture->genTexture(GL_RGBA16F, GL_RGBA, width, height);
        rsmBuffer->bindTexture(outTexture, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);
    }
    rsmBuffer->bindBuffer();
    glViewport(0, 0, width, height);
    auto deferred = RenderManager::GetInstance()->deferredPass;
    normalMap->bind(GL_TEXTURE_2D, 20);
    worldPosMap->bind(GL_TEXTURE_2D, 21);
    fluxMap->bind(GL_TEXTURE_2D, 22);
    deferred->postTexture->bind(GL_TEXTURE_2D, 23);
    deferred->gPosition->bind(GL_TEXTURE_2D, 24);
    deferred->gNormal->bind(GL_TEXTURE_2D, 25);
    deferred->gAlbedoSpec->bind(GL_TEXTURE_2D, 26);
    deferred->gPBR->bind(GL_TEXTURE_2D, 27);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, randomMap);
    indirectShader->use();
    indirectShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    indirectShader->setInt("normalMap", 20);
    indirectShader->setInt("worldPosMap", 21);
    indirectShader->setInt("fluxMap", 22);
    indirectShader->setInt("inTexture", 23);
    indirectShader->setInt("gPosition", 24);
    indirectShader->setInt("gNormal", 25);
    indirectShader->setInt("gAlbedoSpec", 26);
    indirectShader->setInt("gPBR", 27);
    indirectShader->setInt("randomMap", 4);
    indirectShader->setInt("sample_num", glm::clamp(sampleCount, 1, 256));
    indirectShader->setFloat("sample_radius", glm::clamp(sampleRadius, 0.001f, 1.0f));
    indirectShader->setFloat("rsmIntensity", sourceAvailable ? intensity : 0.0f);
    indirectShader->setFloat("minDistance", std::max(minDistance, 0.001f));
    indirectShader->setInt("indirectOnly", indirectOnly ? 1 : 0);
    // Gather from the visible G-buffer instead of rasterizing the scene again.
    glDisable(GL_DEPTH_TEST);
    renderQuad();
    glEnable(GL_DEPTH_TEST);
}
void SSAOPass::initTextures()
{
	gSSAO = std::make_shared<Texture>();
	noiseTexture = std::make_shared<Texture>();
	ssaoFBO = std::make_shared<FrameBuffer>();
}

SSAOPass::SSAOPass()
{
	isDirty = true;
	shaderSSAO = std::make_shared<Shader>("./src/shader/ssao/ssao.vs", "./src/shader/ssao/ssao.fs");
	shaderSSAO->requireMat = false;
	radius = 0.1f;

	initTextures();
	initSSAONoise();
}

void SSAOPass::render()
{
	if(isDirty || InputManager::GetInstance()->viewPortChange){
		
		gSSAO->genTexture(GL_RED, GL_RED, InputManager::GetInstance()->width, InputManager::GetInstance()->height);
		noiseTexture->genTexture(GL_RGBA16F, GL_RGB, 4, 4);
		// initialize noiseTexture
		glBindTexture(GL_TEXTURE_2D, noiseTexture->gpuId());
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 4, 4, 0, GL_RGB, GL_FLOAT, ssaoNoise.data());

		ssaoFBO->bindBuffer();
		ssaoFBO->bindTexture(gSSAO, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D);

		isDirty = false;
	}

	ssaoFBO->bindBuffer();

	glClear(GL_COLOR_BUFFER_BIT);

	auto gPosition = RenderManager::GetInstance()->deferredPass->gPosition;
	auto gNormal = RenderManager::GetInstance()->deferredPass->gNormal;

	glActiveTexture(GL_TEXTURE0);
	// GBuffer's gPosition
	glBindTexture(GL_TEXTURE_2D, gPosition->gpuId());
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gNormal->gpuId());
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, noiseTexture->gpuId());

	shaderSSAO->use();
	shaderSSAO->setInt("gPosition", 0);
	shaderSSAO->setInt("gNormal", 1);
	shaderSSAO->setInt("texNoise", 2);
	shaderSSAO->setFloat("SCR_HEIGHT", InputManager::GetInstance()->height);
	shaderSSAO->setFloat("SCR_WIDTH", InputManager::GetInstance()->width);
	shaderSSAO->setFloat("kernelSize", 64);
	shaderSSAO->setFloat("radius", radius);
	//samples
	for (int i = 0; i < 64;i++){
		shaderSSAO->setVec3("samples[" + std::to_string(i) + "]", ssaoKernel[i]);
	}

	renderQuad();

	ssaoFBO->unbindBuffer();
}

void SSAOPass::initSSAONoise()
{
	randomFloats = std::uniform_real_distribution<float>(0.0, 1.0); // random floats between [0.0, 1.0]
	std::default_random_engine generator;
	for (unsigned int i = 0; i < 64; ++i)
	{
		glm::vec3 sample(
			randomFloats(generator) * 2.0 - 1.0,
			randomFloats(generator) * 2.0 - 1.0,
			randomFloats(generator));

		sample = glm::normalize(sample);
		sample *= randomFloats(generator);
		// better distribution
		float scale = (float)i / 64.0;
		scale = lerp(0.1f, 1.0f, scale * scale);
		sample *= scale;

		ssaoKernel.push_back(sample);
	}
	for (unsigned int i = 0; i < 16; i++)
	{
		glm::vec3 noise(
			randomFloats(generator) * 2.0 - 1.0,
			randomFloats(generator) * 2.0 - 1.0,
			0.0f);
		ssaoNoise.push_back(noise);
	}
}
