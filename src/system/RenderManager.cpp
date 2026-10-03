#include"system/RenderManager.h"
#include"component/Mesh_Renderer.h"
#include"object/Terrain.h"
#include"object/SkyBox.h"
#include"renderer/RenderScene.h"
#include"buffer/UniformBuffer.h"
#include"utils/Camera.h"
#include"component/GameObject.h"
#include"utils/Shader.h"
#include"component/Lights.h"
#include"component/transform.h"
#include"component/TerrainComponent.h"
#include"renderer/RenderPass.h"
#include "renderer/TemporalAA.h"
#include"utils/Utils.h"
#include"component/Atmosphere.h"

#include<glm/gtc/type_ptr.hpp>
#include "renderer/rhi/SceneAdapter.h"
#include "rhi/ShaderAssets.h"
#include "system/InputManager.h"

RenderManager::RenderManager() {
    native_=rhi::usesNativeRenderer();
    setting={true,true,true,false,true,true,true};
    if(native_)return;
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
		false,// enable shadow
		false, // enable rsm
		true, // enable directional
		true, //enable SSAO
	};

}

void RenderManager::init() {
    if(native_){adapter_=std::make_unique<render::SceneAdapter>(rhi::graphicsDevice());return;}
	// UBOs
	initVPbuffer();
	initPointLightBuffer();
	initDirectionLightBuffer();
	initSpotLightBuffer();
	initRenderPass();
}

void RenderManager::initRenderPass() {
    temporalAA=std::make_shared<TemporalAA>();
	// render Pass initialization
	rsmPass = std::make_shared<RSMPass>();
	shadowPass = std::make_shared<ShadowPass>();
	if (setting.useDefer) {
		deferredPass = std::make_shared<DeferredPass>();
		rsmPass = std::make_shared<RSMPass>();
		ssaoPass = std::make_shared<SSAOPass>();
	}
	else {
		postPass = std::make_shared<PostPass>();
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

	const glm::mat4 projection = temporalAA && temporalAA->active() ? temporalAA->projection : camera->GetPerspective();
	const glm::mat4& view = camera->GetViewMatrix();
	const glm::vec3& pos = camera->Position;
	//glm::mat4 skyboxView = glm::mat4(glm::mat3(view));

	// update every frame
	if (uniformVPBuffer) {

		uniformVPBuffer->write(0, sizeof(glm::mat4), glm::value_ptr(projection));
		uniformVPBuffer->write(sizeof(glm::mat4),sizeof(glm::mat4), glm::value_ptr(view));
		uniformVPBuffer->write(128, sizeof(glm::vec3), glm::value_ptr(pos));
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

	int lightNum = scene->pointLights.size();
	int dataSize = 32; // data size for a single light (under std140 layout)
	int index = 0;
	for(auto& light :scene->pointLights){
		if (light) {
			if (!light->dirty) {
				// if not dirty, then pass
				continue;
			}
			PointLightData& data = light->data;
			std::shared_ptr<Transform>&& transform = std::static_pointer_cast<Transform>(
				light->gameObject->GetComponent("Transform"));
			if (transform) {
				uniformPointLightBuffer->write(
					0 + index * dataSize,
					sizeof(glm::vec3), glm::value_ptr(data.color)); //color
				uniformPointLightBuffer->write(
					16 + index * dataSize,
					sizeof(glm::vec3), glm::value_ptr(transform->position)); //position
			}
			++index;
			light->setDirtyFlag(false); // ?
		}
	}
	// add the number of lights to UBO
	uniformPointLightBuffer->write(
		dataSize * 10, sizeof(int), &lightNum);

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
	int dataSize = 48; // data size for a single light (under std140 layout)
	if(!setting.enableDirectional){
		int zero = 0;

		uniformDirectionLightBuffer->write(10 * dataSize,
			sizeof(int), &zero);
		return;
	}

	int lightNum = scene->directionLights.size();
	int index = 0;
	for(auto& light : scene->directionLights){
		if (light) {
			std::shared_ptr<Transform>&& transform = std::static_pointer_cast<Transform>(
				light->gameObject->GetComponent("Transform"));

			if (!light->dirty) {
				continue;
			}
			DirectionLightData& data = light->data;
			uniformDirectionLightBuffer->write(
				0 + index * dataSize,
				sizeof(glm::vec3), glm::value_ptr(data.color)); // ambient
			uniformDirectionLightBuffer->write(
				16 + index * dataSize,
				sizeof(glm::vec3), glm::value_ptr(transform->position)); //
			uniformDirectionLightBuffer->write(
				32 + index * dataSize,
				sizeof(glm::vec3), glm::value_ptr(data.direction));
			//uniformDirectionLightBuffer->write(
				//48 + i * dataSize,
				//sizeof(glm::vec3), glm::value_ptr(data.direction));
			++index;
			light->setDirtyFlag(false);
		}
	}
	uniformDirectionLightBuffer->write(10 * dataSize,
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
			uniformSpotLightBuffer->write(
				0 + i * dataSize,
				sizeof(glm::vec3), glm::value_ptr(data.color)); // ambient
			uniformSpotLightBuffer->write(
				12 + i * dataSize,
				sizeof(float), &data.cutOff);
			uniformSpotLightBuffer->write(
				16 + i * dataSize,
				sizeof(glm::vec3), glm::value_ptr(transform->position)); //
			uniformSpotLightBuffer->write(
				28 + i * dataSize,
				sizeof(float), &data.outerCutOff);
			light->setDirtyFlag(false);
			uniformSpotLightBuffer->write(
				32 + i * dataSize,
				sizeof(glm::vec3), glm::value_ptr(data.direction));
		}
	}
	uniformSpotLightBuffer->write(10 * dataSize,
		sizeof(int), &lightNum);
}

void RenderManager::prepareCompData(const std::shared_ptr<RenderScene>& scene) {
	// compute terrain
	if (scene->terrain) {
		scene->terrain->constructCall();
	}
	if (scene->sky) {
		auto&& atmosphere = std::static_pointer_cast<Atmosphere>(scene->sky->GetComponent("Atmosphere"));
		atmosphere->constructCall();
	}
}

rhi::TextureHandle RenderManager::output()const{return renderer_?renderer_->output():rhi::TextureHandle{};}
void RenderManager::releaseNative(){renderer_.reset();adapter_.reset();}
void RenderManager::render(const std::shared_ptr<RenderScene>& scene) {
    if(native_){
        const auto input=InputManager::GetInstance();if(input->width<=0 || input->height<=0)return;
        if(!renderer_)renderer_=std::make_unique<render::ForwardPbrRenderer>(rhi::graphicsDevice(),rhi::defaultShaderDirectory(),input->width,input->height,render::PbrPath::Scene);
        renderer_->resize(input->width,input->height);if(scene->main_camera)scene->main_camera->aspect_ratio=float(input->width)/input->height;
        auto frame=adapter_->collect(scene,setting.timeOverride);frame.frame.shadows=setting.enableShadow;frame.frame.ssao=setting.enableSSAO;frame.frame.rsm=setting.enableRSM;frame.frame.taa=setting.enableTSAA;frame.frame.aoRadius=setting.aoRadius;frame.frame.aoBias=setting.aoBias;frame.frame.aoPower=setting.aoPower;frame.frame.toneMapping=setting.enableHDR;frame.frame.rsmSettings=setting.rsmSettings;frame.frame.directionalEnabled=setting.enableDirectional;frame.frame.forwardShading=!setting.useDefer;
        if(!setting.enableDirectional)for(auto& light:frame.frame.lights)if(light.positionType.w==0)light.colorInner=glm::vec4(0);
        renderer_->render(frame.frame,frame.packets,frame.exposure);return;
    }
	// Outdoor RSM uses the sun/sky; indoor scenes can fall back to a spotlight.
	if (scene->spotLights.empty() && scene->directionLights.empty() && !scene->sky) setting.enableRSM = false;
    temporalAA->begin(scene,setting.enableTSAA && setting.useDefer);
	prepareVPData(scene);
	glCheckError();

	preparePointLightData(scene);
	glCheckError();

	prepareDirectionLightData(scene);
	glCheckError();

	prepareSpotLightData(scene);
	glCheckError();

	prepareCompData(scene);
	glCheckError();

	//TODO:
	 //rsmPass->renderGbuffer(scene);

	//shadow pass
	if (setting.enableShadow) {
		shadowPass->render(scene);
		pass_data();
	}
	if (setting.useDefer) {
		// deferred pass
		deferredPass->renderGbuffer(scene);
		glCheckError();

		if(setting.enableSSAO){
			ssaoPass->render();
		}

		deferredPass->render(scene);
		glCheckError();

		if (setting.enableRSM) {
			rsmPass->renderGbuffer(scene);
			rsmPass->render(scene);
		}
		deferredPass->renderAlphaObjects(scene);
		glCheckError();
		deferredPass->postProcess(scene);
		glCheckError();
	}
	else
	{
		// depth pass (camera space)
		depthPass->render(scene);

		// base pass
		if (setting.enableHDR) {
			postPass->bindBuffer();
		}
		else {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		basePass->render(scene, nullptr);

		// hdr pass
		if (setting.enableHDR) {
			postPass->render();
		}
	}
}

std::shared_ptr<Shader> RenderManager::getShader(ShaderType type) {
    if(native_)return nullptr;
	int index = static_cast<int>(type);
	//if(!m_shader[index]){
	//	//if not initialized
	//	m_shader[index] = RenderManager::generateShader(type);
	//}
	//
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
	this->deferredPass->cascadedMatrixBuffer = this->shadowPass->getMatrixBuffer();
	this->deferredPass->shadow_limiter = this->shadowPass->get_shadow_limiter();
}
