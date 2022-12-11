#include"RenderManager.h"
#include"../renderer/Mesh_Renderer.h"
#include"../object/Terrain.h"
#include"../object/SkyBox.h"
#include"../renderer/RenderScene.h"
#include"../buffer/UniformBuffer.h"
#include"../utils/Camera.h"
#include"../component/GameObject.h"
#include"../utils/Shader.h"
#include"../component/Lights.h"
#include"../component/transform.h"
#include"../component/TerrainComponent.h"
#include"../renderer/RenderPass.h"
#include"../utils/Utils.h"
#include"../component/Atmosphere.h"

#include<glm/gtc/type_ptr.hpp>

RenderManager::RenderManager() {
	int ShaderTypeNum = static_cast<int>(ShaderType::KIND_COUNT);
	m_shader = std::vector<std::shared_ptr<Shader>>(ShaderTypeNum,nullptr);
	// init Shaders
	for (int i = 0; i < ShaderTypeNum; i++) {
		m_shader[i] = generateShader(ShaderType(i));
	}

	// setting
	setting = RenderSetting{
		true, // enableHDR
		true, //useDeferred
		true// enable shadow
	};

}

void RenderManager::init() {
	// UBOs 
	initVPbuffer();
	initPointLightBuffer();
	initDirectionLightBuffer();
	initSpotLightBuffer();
	initRenderPass();
}

void RenderManager::initRenderPass() {
	// render Pass initialization 
	rsmPass = std::make_shared<RSMPass>();
	shadowPass = std::make_shared<ShadowPass>();
	if (setting.useDefer) {
		deferredPass = std::make_shared<DeferredPass>();
		rsmPass = std::make_shared<RSMPass>();
	}
	else {
		hdrPass = std::make_shared<HDRPass>();
		basePass = std::make_shared<BasePass>();
		depthPass = std::make_shared<DepthPass>();
	}
}

void RenderManager::initVPbuffer() {
	// initialize a UBO for VP matrices
	uniformVPBuffer = std::make_shared<UniformBuffer>(
			2 * sizeof(glm::mat4) + 2* sizeof(glm::vec3)
		); 
	// set binding point
	uniformVPBuffer->setBinding(0);
}

void RenderManager::initPointLightBuffer() {
	const int maxLight = 10; 
	int lightBufferSize = maxLight * (32) + 16; // maximum 10 point lights, std140 layout
	uniformPointLightBuffer = std::make_shared<UniformBuffer>(lightBufferSize);
	// set binding point
	uniformPointLightBuffer->setBinding(1);
}

void RenderManager::initDirectionLightBuffer() {
	const int maxLight = 10; 
	int lightBufferSize = maxLight * (48) + 16; 
	uniformDirectionLightBuffer = std::make_shared<UniformBuffer>(lightBufferSize);
	// set binding point
	uniformDirectionLightBuffer->setBinding(2);
}

void RenderManager::initSpotLightBuffer() {
	const int maxLight = 10;
	int lightBufferSize = maxLight * (48) + 16;
	uniformSpotLightBuffer = std::make_shared<UniformBuffer>(lightBufferSize);
	// set binding point
	uniformSpotLightBuffer->setBinding(3);
}

RenderManager::~RenderManager() {

}

void RenderManager::prepareVPData(const std::shared_ptr<RenderScene>& renderScene) {
	const std::shared_ptr<Camera>& camera = renderScene->main_camera;
	if (camera == nullptr) {
		return;
	}

	const glm::mat4& projection = camera->GetPerspective();
	const glm::mat4& view = camera->GetViewMatrix();
	const glm::vec3& pos = camera->Position;
	//glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
	
	// update every frame
	if (uniformVPBuffer) {
		uniformVPBuffer->bindBuffer();
		GLuint UBO = uniformVPBuffer->UBO;
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(projection));
		glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4),sizeof(glm::mat4), glm::value_ptr(view));
		glBufferSubData(GL_UNIFORM_BUFFER, 128, sizeof(glm::vec3), glm::value_ptr(pos));
	}

	// update every frame: skybox view
	glm::mat4 view_for_skybox = glm::mat4(glm::mat3(camera->GetViewMatrix()));
	std::shared_ptr<Shader>& skyboxShader = m_shader[static_cast<int>(ShaderType::SKYBOX)];
	if (skyboxShader) {
		skyboxShader->use();
		skyboxShader->setMat4("view", view_for_skybox);
	}

	std::shared_ptr<Shader>& skyShader = m_shader[static_cast<int>(ShaderType::SKY)];
	if (skyShader) {
		skyShader->use();
		skyShader->setMat4("view", view_for_skybox);
	}

	// campos
	for (auto& shader : m_shader) {
		if (shader) { 
			shader->use();
			shader->setVec3("camPos", renderScene->main_camera->Position); 
		}
	}
}

void RenderManager::preparePointLightData(const std::shared_ptr<RenderScene>& scene) {
	// point light
	// update at the first time
	//if (uniformPointLightBuffer->dirty) {
	//	uniformPointLightBuffer->dirty = false;
	//	for (std::shared_ptr<Shader>& shader : m_shader) {
	//		if (shader) {
	//			shader->use();
	//			shader->setUniformBuffer("PointLightBuffer", uniformPointLightBuffer->binding);
	//		}
	//	}
	//}
	uniformPointLightBuffer->bindBuffer();
	unsigned int UBO = uniformPointLightBuffer->UBO;
	int lightNum = scene->pointLights.size();
	int dataSize = 32; // data size for a single light (under std140 layout)
	for (int i = 0; i < lightNum; i++) {
		auto& light = scene->pointLights[i]; 
		if (light) {
			if (!light->dirty) {
				// if not dirty, then pass
				continue;
			}
			PointLightData& data = light->data;
			std::shared_ptr<Transform>&& transform = std::static_pointer_cast<Transform>(
				light->gameObject->GetComponent("Transform"));
			if (transform) {
				glBufferSubData(GL_UNIFORM_BUFFER,
					0 + i * dataSize,
					sizeof(glm::vec3), glm::value_ptr(data.color)); //color 
				glBufferSubData(GL_UNIFORM_BUFFER,
					16 + i * dataSize,
					sizeof(glm::vec3), glm::value_ptr(transform->position)); //position
			}
			light->setDirtyFlag(false); // ?
		}
	}
	// add the number of lights to UBO
	glBufferSubData(GL_UNIFORM_BUFFER,
		dataSize * 10, sizeof(int), &lightNum);

	//uniformPointLightBuffer->bindBuffer();
}

void RenderManager::prepareDirectionLightData(const std::shared_ptr<RenderScene>& scene) {
	// update light data only when dirty 
	//if (uniformDirectionLightBuffer->dirty) {
	//	uniformDirectionLightBuffer->dirty = false;
	//	for (std::shared_ptr<Shader>& shader : m_shader) {
	//		if (shader) {
	//			shader->setUniformBuffer("DirectionLightBuffer", uniformDirectionLightBuffer->binding);
	//		}
	//	}
	//}
	uniformDirectionLightBuffer->bindBuffer();
	unsigned int UBO = uniformDirectionLightBuffer->UBO;
	int lightNum = scene->directionLights.size();
	int dataSize = 48; // data size for a single light (under std140 layout)
	for (int i = 0; i < lightNum; i++) {
		auto& light = scene->directionLights[i];
		if (light) {
			std::shared_ptr<Transform>&& transform = std::static_pointer_cast<Transform>(
				light->gameObject->GetComponent("Transform"));

			if (!light->dirty) {
				continue;
			}
			DirectionLightData& data = light->data;
			glBufferSubData(GL_UNIFORM_BUFFER,
				0 + i * dataSize,
				sizeof(glm::vec3), glm::value_ptr(data.color)); // ambient
			glBufferSubData(GL_UNIFORM_BUFFER,
				16 + i * dataSize,
				sizeof(glm::vec3), glm::value_ptr(transform->position)); //
			glBufferSubData(GL_UNIFORM_BUFFER,
				32 + i * dataSize,
				sizeof(glm::vec3), glm::value_ptr(data.direction));
			//glBufferSubData(GL_UNIFORM_BUFFER, 
				//48 + i * dataSize, 
				//sizeof(glm::vec3), glm::value_ptr(data.direction));
			light->setDirtyFlag(false);
		}
	}
	glBufferSubData(GL_UNIFORM_BUFFER, 10 * dataSize, 
		sizeof(int), &lightNum);
}

void RenderManager::prepareSpotLightData(const std::shared_ptr<RenderScene>& scene) {
	// update light data only when dirty 
	//if (uniformSpotLightBuffer->dirty) {
	//	uniformSpotLightBuffer->dirty = false;
	//	for (std::shared_ptr<Shader>& shader : m_shader) {
	//		if (shader) {
	//			shader->setUniformBuffer("DirectionLightBuffer", uniformSpotLightBuffer->binding);
	//		}
	//	}
	//}
	uniformSpotLightBuffer->bindBuffer();
	unsigned int UBO = uniformSpotLightBuffer->UBO;
	int lightNum = scene->spotLights.size();
	int dataSize = 48; // data size for a single light (under std140 layout)
	for (int i = 0; i < lightNum; i++) {
		auto& light = scene->spotLights[i];
		if (light) {
			std::shared_ptr<Transform>&& transform = std::static_pointer_cast<Transform>(
				light->gameObject->GetComponent("Transform"));

			if (!light->dirty) {
				continue;
			}
			SpotLightData& data = light->data;
			glBufferSubData(GL_UNIFORM_BUFFER,
				0 + i * dataSize,
				sizeof(glm::vec3), glm::value_ptr(data.color)); // ambient
			glBufferSubData(GL_UNIFORM_BUFFER,
				12 + i * dataSize,
				sizeof(float), &data.cutOff);
			glBufferSubData(GL_UNIFORM_BUFFER,
				16 + i * dataSize,
				sizeof(glm::vec3), glm::value_ptr(transform->position)); //
			glBufferSubData(GL_UNIFORM_BUFFER,
				28 + i * dataSize,
				sizeof(float), &data.outerCutOff);
			light->setDirtyFlag(false);
			glBufferSubData(GL_UNIFORM_BUFFER,
				32 + i * dataSize,
				sizeof(glm::vec3), glm::value_ptr(data.direction));
		}
	}
	glBufferSubData(GL_UNIFORM_BUFFER, 10 * dataSize,
		sizeof(int), &lightNum);
}

void RenderManager::prepareCompData(const std::shared_ptr<RenderScene>& scene) {
	// compute terrain
	if (scene->terrain) {
		scene->terrain->constructCall();
	}
	if (scene->sky) {
		auto& atmosphere = std::static_pointer_cast<Atmosphere>(scene->sky->GetComponent("Atmosphere"));
		atmosphere->constructCall();
	}
}

void RenderManager::render(const std::shared_ptr<RenderScene>& scene) {
	prepareVPData(scene);
	glCheckError();
	preparePointLightData(scene);
	glCheckError();
	prepareDirectionLightData(scene);
	glCheckError();
	prepareSpotLightData(scene);
	glCheckError();
	prepareCompData(scene);
	
	//TODO:
	//  rsmPass->render(scene);

	//shadow pass
	if (setting.enableShadow) {
		shadowPass->render(scene);
		pass_data();
	}
	if (setting.useDefer) {
		// deferred pass
		deferredPass->renderGbuffer(scene);
		deferredPass->render(scene);
		deferredPass->postProcess(scene);
	}
	else 
	{
		// depth pass (camera space)
		depthPass->render(scene);

		// base pass 
		if (setting.enableHDR) {
			hdrPass->bindBuffer();
		}
		basePass->render(scene, nullptr);

		// hdr pass 
		if (setting.enableHDR) {
			hdrPass->render();
		}
	}
}

std::shared_ptr<Shader> RenderManager::getShader(ShaderType type) {
	int index = static_cast<int>(type);
	//if(!m_shader[index]){
	//	//if not initialized 
	//	m_shader[index] = RenderManager::generateShader(type);
	//}
	//glCheckError();
	return m_shader[index];
}

std::shared_ptr<Shader> RenderManager::generateShader(ShaderType type) {

	switch (type) {
		case ShaderType::LIGHT:
			return std::make_shared<Shader>(
				"./src/shader/light.vs", "./src/shader/light.fs", nullptr,
				nullptr, nullptr
				);
			break;
		case ShaderType::PBR_TESS:
			return std::make_shared<Shader>(
				"./src/shader/pbr/pbr_tess.vs", "./src/shader/pbr/pbr.fs",nullptr,// "./src/shader/pbr/pbr.gs",
				"./src/shader/pbr/pbr.tesc","./src/shader/pbr/pbr.tese"
				);
			break;
		case ShaderType::PBR_ANISOTROPY:
			return std::make_shared<Shader>(
				"./src/shader/pbr/pbr.vs", "./src/shader/pbr/anisotropic.fs"
				);
			break;
		case ShaderType::PBR:
			return std::make_shared<Shader>(
				"./src/shader/pbr/pbr.vs","./src/shader/pbr/pbr.fs",nullptr//"./src/shader/pbr/pbr.gs"
				);
			break;
		case ShaderType::SIMPLE:
			return std::make_shared<Shader>(
				"./src/shader/simple.vs", "./src/shader/simple.fs"
				);
			break;
		case ShaderType::SKYBOX:
			return std::make_shared<Shader>(
				"./src/shader/skybox.vs", "./src/shader/skybox.fs"
				);
			break;
		case ShaderType::TERRAIN:
			//return std::make_shared<Shader>(
				//"./src/shader/terrain.vs", "./src/shader/pbr.fs", nullptr,
				//"./src/shader/terrain.tesc", "./src/shader/terrain.tese"
				//);
			return std::make_shared<Shader>(
				"./src/shader/terrain/terrain.vs","./src/shader/pbr/pbr.fs"
				//"./src/shader/terrain/terrain.vs","./src/shader/terrain/terrain.fs"
				);
			break;
		case ShaderType::HDR:
			return std::make_shared<Shader>(
				"./src/shader/hdr.vs","./src/shader/hdr.fs"
				);
			break;
		case ShaderType::SKY:
			return std::make_shared<Shader>(
				"./src/shader/sky/skyRender.vs","./src/shader/sky/skyRender.fs"
				);
			break;
		case ShaderType::TEST:
			return std::make_shared<Shader>(
				"./src/shader/test.vs","./src/shader/test.fs"
				);
			break;
		case ShaderType::PBR_CLEARCOAT:
			return std::make_shared<Shader>(
				"./src/shader/pbr/pbr.vs","./src/shader/pbr/clearcoat.fs"
				);
			break;
		case ShaderType::PBR_SSS:
			return std::make_shared<Shader>(
				"./src/shader/pbr/pbr.vs","./src/shader/pbr/sss.fs"
				);
			break;
		case ShaderType::DEPTH:
			return std::make_shared<Shader>(
				"./src/shader/shadow/depth.vs","./src/shader/shadow/depth.fs"
				);
		default:
			std::cerr << "No such shader type" << '\n';
			break;
	}
	//return std::make_shared<Shader>(nullptr, nullptr, nullptr, nullptr, nullptr);
}

void RenderManager::pass_data()
{
	this->deferredPass->cascaded_matrix_UBO = this->shadowPass->get_UBO();
	this->deferredPass->shadow_limiter = this->shadowPass->get_shadow_limiter();
}
