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
#include"../component/transform.h"
#include"../buffer/RenderBuffer.h"
#include<memory>
#include<random>

extern std::unique_ptr<InputManager> inputManager;
extern std::unique_ptr<RenderManager> renderManager;

void BasePass::render(const std::shared_ptr<RenderScene>& scene,const std::shared_ptr<Shader>& outShader) {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, inputManager->width, inputManager->height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

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
	shadowShader_dir =std::make_shared<Shader>("./src/shader/shadow/cascaded_shadow_depth.vs","./src/shader/shadow/cascaded_shadow_depth.fs","./src/shader/shadow/cascaded_shadow_depth.gs");
	shadowShader_dir->requireMat = false;
	shadowShader_point = std::make_shared<Shader>("./src/shader/shadow/point_shadow_depth.vs", "./src/shader/shadow/point_shadow_depth.fs", "./src/shader/shadow/point_shadow_depth.gs");
	shadowShader_point->requireMat = false;

	// TODO: init framebuffer
	//for creating multiple depth attachment for a fb is not allowed

	//set UBO
	glGenBuffers(1, &matrixUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, matrixUBO);

	glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4)*10*cascaded_layers, nullptr, GL_STATIC_DRAW);  
	//suppose 5 is the cascaded levels, 10 is the max num of directional lights
	// this way is kinda undecent ,but convenient.


	glBindBufferBase(GL_UNIFORM_BUFFER, 5, matrixUBO);  //5 is the binding point
	glBindBuffer(GL_UNIFORM_BUFFER,0);
}

ShadowPass::~ShadowPass()
{
	if (matrixUBO)
		glDeleteBuffers(1, &matrixUBO);
}

void ShadowPass::render(const std::shared_ptr<RenderScene>& scene) {
	// TODO: 
	
	// rendering shadow map
	pointLightShadow(scene);
	directionLightShadow(scene); //after evoke these 2 functions, the depth maps are restored in the lights' texes.
}

void ShadowPass::pointLightShadow(const std::shared_ptr<RenderScene>& scene) {
	//TODO:
	auto& plights = scene->pointLights;
	unsigned int num_point_lights = plights.size();
	for (unsigned int i=0;i<num_point_lights;i++)
	{
		const auto& light = plights.at(i);
		if (!light || !light->castShadow) {
			continue;
		}
		if (dirty)
			init_framebuffers(scene);

		shadowShader_point->use();
		const auto& current_framebuffer = frameBuffer_points.at(i);

		glCheckError();
		current_framebuffer->bindBuffer();
		//glEnable(GL_DEPTH_TEST);
		//glEnable(GL_CULL_FACE);

		//current_framebuffer->bindShadowTexture(light->shadowTex, GL_DEPTH_ATTACHMENT);
		//glDrawBuffer(GL_NONE);
		//glReadBuffer(GL_NONE);
		//
		// start to render
		float near_plane = scene->main_camera->zNear;
		float far_plane = scene->main_camera->zFar;
		
		glm::mat4 proj = glm::perspective(glm::radians(90.0f), (float)cube_map_resolution / cube_map_resolution, near_plane, far_plane);

		std::vector<glm::mat4> omnishadow_matrices;
		for (unsigned i = 0; i < 6; i++) {
			auto&& transforms = light->getLightTransform(i);
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
		auto trans = std::static_pointer_cast<Transform>(light->gameObject->GetComponent("Transform"));
		lightPos=trans->position;
		shadowShader_point->setFloat("far_plane", far_plane);
		shadowShader_point->setVec3("lightPos", lightPos);
		glCheckError();
		for (auto& object : scene->objects)
		{
			std::shared_ptr<MeshRenderer>&& renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
			if (renderer && renderer->drawMode == GL_TRIANGLES) { // due to geometry shader, drawing points is not allowed;
				renderer->render(shadowShader_dir);
			}
		}
		glCheckError();

	}
}

void ShadowPass::directionLightShadow(const std::shared_ptr<RenderScene>& scene) {
	//TODO:
	auto& dLights = scene->directionLights;
	unsigned int num_direction_lights = dLights.size();
	for (unsigned int i = 0; i < num_direction_lights;i++) 
	{
		const auto& light = dLights.at(i);
		if (!light || !light->castShadow) 
		{
			continue;
		}
		if (dirty)
			init_framebuffers(scene);

		const auto& current_framebuffer = frameBuffer_dirs.at(i);
		current_framebuffer->bindBuffer();
		//glEnable(GL_DEPTH_TEST);
		
		shadowShader_dir->use();
		//current_framebuffer->bindShadowTexture(light->shadowTex, GL_DEPTH_ATTACHMENT);
		//glDrawBuffer(GL_NONE);
		//glReadBuffer(GL_NONE);


		//send the matrices into the uniform variable in shaders
		std::vector<glm::mat4> light_matrices = get_stratified_matrices(scene, light);


		//binding an UBO
		glBindBuffer(GL_UNIFORM_BUFFER, matrixUBO);
		for (unsigned j = 0; j < light_matrices.size(); j++)
		{
			// i-th light j-th level
			glBufferSubData(GL_UNIFORM_BUFFER, (i*cascaded_layers+j)* sizeof(glm::mat4), sizeof(glm::mat4), &light_matrices[i]);
		}
		//glBindBuffer(GL_UNIFORM_BUFFER, 5);  // here we bind the matrices of all dir lights ,of all levels in the 5 binding points of uniform buffers
		/*******/
		
		// for (unsigned i = 0; i < light_matrices.size(); i++)
		// {
		// 	shadowShader_dir->setMat4("lightSpaceMatrices[" + std::to_string(i) + "]", light_matrices.at(i));
		// }

		glViewport(0, 0, this->cascaded_map_resolution, this->cascaded_map_resolution);
		glClear(GL_DEPTH_BUFFER_BIT);
		glCullFace(GL_FRONT);

		//rendering
		glCheckError();
		for (auto object : scene->objects) {
			std::shared_ptr<MeshRenderer>&& renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
			if (renderer && renderer->drawMode == GL_TRIANGLES) { // due to implemetation of geometry shader, drawing points is not allowed
				renderer->render(shadowShader_dir);
			}
		}

		glCullFace(GL_BACK);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}

std::vector<glm::vec4> ShadowPass::get_frustum_points(const float nearplane, const float farplane, const std::shared_ptr<RenderScene>& scene)
{
	std::vector<glm::vec4> re;
	const auto& camera = scene->main_camera;
	glm::mat4 perspective = glm::perspective(glm::radians(camera->Zoom), camera->aspect_ratio, nearplane, farplane);
	glm::mat4 view = camera->GetViewMatrix();

	glm::mat4 inv = glm::inverse(perspective * view);
	for (unsigned z = 0; z < 2; z++)
		for (unsigned y = 0; y < 2; y++)
			for (unsigned x = 0; x < 2; x++)
			{
				glm::vec4 tmp = inv*glm::vec4(2.0 * x - 1.0, 2.0 * y - 1.0, 2.0 * z - 1.0, 1.0);
				re.emplace_back(tmp/tmp.w);
			}
	return re;
}

glm::mat4 ShadowPass::get_stratified_matrix(const std::vector<glm::vec4>& points, const std::shared_ptr<DirectionLight>& light)
{
	std::vector<glm::mat4> re;
	glm::vec3 center=glm::vec3(0.0,0.0,0.0);
	for (const auto& i : points)
	{
		center += glm::vec3(i);
	}

	center /= points.size();  // the center of the frustum in world space
	auto light_view = glm::lookAt(center + light->data.direction, center, glm::vec3(0.0, 1.0, 0.0));

	float minX = std::numeric_limits<float>::min();
	float maxX = std::numeric_limits<float>::max();
	float minY = std::numeric_limits<float>::min();
	float maxY = std::numeric_limits<float>::max();
	float minZ = std::numeric_limits<float>::min();
	float maxZ = std::numeric_limits<float>::max();


	
	for (const auto& i : points)
	{
		auto frustm_point_light_space = light_view * i;
		minX = frustm_point_light_space.x < minX ? frustm_point_light_space.x : minX;
		maxX = frustm_point_light_space.x > maxX ? frustm_point_light_space.x : maxX;
		minY = frustm_point_light_space.y < minY ? frustm_point_light_space.y : minY;
		maxY = frustm_point_light_space.y > maxY ? frustm_point_light_space.y : maxY;
		minZ = frustm_point_light_space.z < minZ ? frustm_point_light_space.z : minZ;
		maxZ = frustm_point_light_space.z > maxZ ? frustm_point_light_space.z : maxZ;
	}

	//EXPAND z
	float z_ratio = 10.0;
	minZ = minZ < 0 ? 1.0*minZ*z_ratio : 1.0*minZ / z_ratio;
	maxZ = maxZ < 0 ? 1.0*maxZ / z_ratio : 1.0*maxZ * z_ratio;


	const glm::mat4 light_proj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

	return light_proj * light_view;

}

std::vector<glm::mat4>ShadowPass:: get_stratified_matrices(const std::shared_ptr<RenderScene>& scene,const std::shared_ptr<DirectionLight> light)
{
	std::vector<glm::mat4> re;
	float camera_near=scene->main_camera->zNear, camera_far=scene->main_camera->zFar;
	shadow_limiter.at(0) = camera_far / 50.0;
	shadow_limiter.at(1) = camera_far / 25.0;
	shadow_limiter.at(2) = camera_far / 10.0;
	shadow_limiter.at(3) = camera_far / 2.0;
	
	float near, far;
	for (unsigned i = 0; i < 5; i++)
	{
		near = i == 0 ? camera_near : shadow_limiter.at(i-1);
		far = i == 4 ? camera_far : shadow_limiter.at(i);
		std::vector<glm::vec4> frustum_points = get_frustum_points(near, far, scene);
		glm::mat4 light_matrix = get_stratified_matrix(frustum_points, light);
		re.emplace_back(light_matrix);
	}
	return re;
}

void ShadowPass::init_framebuffers(const std::shared_ptr<RenderScene>& scene)
{
	unsigned int num_direction_lights = scene->directionLights.size();
	unsigned int num_point_lights = scene->pointLights.size();

	glCheckError();
	for (const auto& light : scene->directionLights)
	{
		light->shadowTex->genTextureArray(
			GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, cascaded_map_resolution, cascaded_map_resolution, 0, cascaded_layers
		);

		auto new_framebuffer = std::make_shared<FrameBuffer>();
		new_framebuffer->bindBuffer();
		new_framebuffer->bindShadowTexture(light->shadowTex, GL_DEPTH_ATTACHMENT);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		//add to vectors
		frameBuffer_dirs.emplace_back(new_framebuffer);
	}
	for (const auto& light : scene->pointLights)
	{
		light->shadowTex->genCubeMap(GL_DEPTH_COMPONENT, cube_map_resolution, cube_map_resolution);
		auto new_framebuffer = std::make_shared<FrameBuffer>();
		new_framebuffer->bindBuffer();
		new_framebuffer->bindShadowTexture(light->shadowTex, GL_DEPTH_ATTACHMENT);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		//add to vectors
		frameBuffer_points.emplace_back(new_framebuffer);
	}
	glCheckError();
	dirty = false;
	//once we generate these framebuffers, we set dirty as true to avoid repeatedly do these procedures in every pass
}

unsigned int ShadowPass::get_UBO()const
{
	return this->matrixUBO;
}

std::vector<float> ShadowPass::get_shadow_limiter() const
{
	return this->shadow_limiter;
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
	//bindings
	glBindBufferBase(GL_UNIFORM_BUFFER, 5, cascaded_matrix_UBO);

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
	glCheckError();
	
	unsigned base = 4;// already 4 texture units occupied

	//int i = 0;
	for (int i = 0; i < scene->directionLights.size();++i)
	{
		int texture_unit_index = i + base;
		glActiveTexture(GL_TEXTURE0 + texture_unit_index);
		glBindTexture(GL_TEXTURE_2D_ARRAY, scene->directionLights.at(i)->shadowTex->id);
		lightingShader->setInt("shadow_maps[" + std::to_string(i) + "]", texture_unit_index);
	}
	glCheckError();

	base += scene->directionLights.size();
	for (int i = 0; i < scene->pointLights.size(); ++i)
	{
		int texture_unit_index = i + base;
		glActiveTexture(GL_TEXTURE0 + texture_unit_index);
		glBindTexture(GL_TEXTURE_CUBE_MAP, scene->pointLights.at(i)->shadowTex->id);
		lightingShader->setInt("shadow_cubes[" + std::to_string(i) + "]", texture_unit_index);
	}
	glCheckError();

	lightingShader->setInt("gPosition", 0);
	lightingShader->setInt("gNormal", 1);
	lightingShader->setInt("gAlbedoSpec", 2);
	lightingShader->setInt("gPBR", 3);

	lightingShader->setFloat("far_plane", scene->main_camera->zFar);
	lightingShader->setInt("cascaded_levels", 4);
	
	for (unsigned int i = 0; i < 4; i++)
		lightingShader->setFloat("cascaded_distances[" + std::to_string(i) + "]", shadow_limiter[i]);
	glCheckError();
	
	// set uniforms
	if (scene->main_camera) {
		//lightingShader->setVec3("camPos", scene->main_camera->Position);	

		lightingShader->setFloat("far_plane", scene->main_camera->zFar);
		lightingShader->setInt("cascaded_levels", 5);
	}
	glCheckError();

	// render quad
	renderQuad();

	glCheckError();
	// copy renderbuffer
	glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer->FBO);
	//glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postBuffer->FBO);
	int width = inputManager->width;
	int height = inputManager->height;
	glBlitFramebuffer(0, 0, width, height,
		0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	glCheckError();

	//glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

RSMPass::RSMPass() {
	initShader();
	initTextures();
}

RSMPass::~RSMPass() {

}
void RSMPass::initShader() {
	RSMShader = std::make_shared<Shader>("./src/shader/rsm/lightSpace.vs", "./src/shader/rsm/lightSpace.fs");
	RSMShader->requireMat = true;

	//RSMShader->use();
}

GLuint RSMPass::createRandomTexture(int size) {
	std::default_random_engine eng;
	std::uniform_real_distribution<float> dist(0.0f, 1.0f);
	eng.seed(std::time(0));
	float PI = std::cos(-1.0f);
	glm::vec3* randomData = new glm::vec3[size];
	for (int i = 0; i < size; ++i) {
		float r1 = dist(eng);
		float r2 = dist(eng);
		randomData[i].x = r1 * std::sin(2 * PI * r2);
		randomData[i].y = r1 * std::cos(2 * PI * r2);
		randomData[i].z = r1 * r1;
	}
	GLuint randomTexture;
	glGenTextures(1, &randomTexture);
	glBindTexture(GL_TEXTURE_2D, randomTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, size, 1, 0, GL_RGB, GL_FLOAT, randomData);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	delete[] randomData;
	return randomTexture;
}
void RSMPass::initTextures() {
	rsmFBO = std::make_shared<FrameBuffer>();

	depthMap = std::make_shared<Texture>();
	normalMap = std::make_shared<Texture>();
	worldPosMap = std::make_shared<Texture>();
	fluxMap = std::make_shared<Texture>();
	GLuint randomMap = createRandomTexture();
	depthMap->genTexture(GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, RSM_WIDTH, RSM_HEIGHT);
	normalMap->genTexture(GL_RGBA16F, GL_RGBA, RSM_WIDTH, RSM_HEIGHT);
	worldPosMap->genTexture(GL_RGBA16F, GL_RGBA, RSM_WIDTH, RSM_HEIGHT);
	fluxMap->genTexture(GL_RGBA16F, GL_RGBA, RSM_WIDTH, RSM_HEIGHT);
}

void RSMPass::render(const std::shared_ptr<RenderScene>& scene) {
	if (rsmFBO->dirty) {
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
			GL_COLOR_ATTACHMENT2
		};
		glDrawBuffers(3, attachments);
	}

	rsmFBO->bindBuffer();
	glClear(GL_DEPTH_BUFFER_BIT);
	glViewport(0, 0, RSM_WIDTH, RSM_HEIGHT);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	//TODO:set light uniform
	auto light = scene->spotLights[0];
	auto trans = std::static_pointer_cast<Transform>(light->gameObject->GetComponent("Transform"));
	glm::vec3 lightPos = trans->position;
	glm::mat4 lightProjection = glm::perspective(glm::radians(60.0f), (float)RSM_WIDTH / (float)RSM_HEIGHT, light->near, light->far);
	glm::mat4 lightView = glm::lookAt(lightPos, lightPos + light->data.direction, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 lightSpaceMatrix = lightProjection * lightView;
	RSMShader->use();
	RSMShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
	RSMShader->setVec3("light.Position", lightPos);
	RSMShader->setVec3("light.Color", light->data.color);
	RSMShader->setVec3("light.Direction", light->data.direction);

	for (int i = 0; i < scene->objects.size(); i++) {
		auto& object = scene->objects[i];
		//// only render deferred objects in gbuffer phase
		//if (object->isDeferred()) {
		std::shared_ptr<MeshRenderer>&& renderer = std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer && renderer->shader) {
			renderer->render(RSMShader);
		}
		//}
	}

	//std::shared_ptr<Terrain>& terrain = scene->terrain;
	//if (terrain) {
	//	//terrain->shader->use();
	//	glCheckError();
	//	//auto& terrainComponent = std::static_pointer_cast<TerrainComponent>(terrain->GetComponent("TerrainComponent"));
	//	//if (terrainComponent) {
	//		//terrainComponent->render(terrainComponent->terrainGBuffer);
	//	//}
	//	terrain->render(nullptr);
	//}
	// no sky
}
