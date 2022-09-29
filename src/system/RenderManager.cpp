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

#include<glm/gtc/type_ptr.hpp>

RenderManager::RenderManager() {
	m_shader = std::vector<std::shared_ptr<Shader>>(ShaderTypeNum,nullptr);
	initVPbuffer();
	initPointLightBuffer();
}
void RenderManager::initVPbuffer() {
	// initialize a UBO for VP matrices
	uniformVPBuffer = std::make_shared<UniformBuffer>(
			2 * sizeof(glm::mat4)
		);
	uniformVPBuffer->setBinding(0);
}
void RenderManager::initPointLightBuffer() {
	// TODO: 
	const int maxLight = 10; 
	int lightBufferSize = maxLight * (32) + 16; // maximum 10 point lights, std140 layout

	uniformPointLightBuffer = std::make_shared<UniformBuffer>(lightBufferSize);
}
void RenderManager::initDirectionLightBuffer() {
	// TODO:
	const int maxLight = 10; 
	int lightBufferSize = maxLight * (48) + 16; 
	uniformDirectionLightBuffer = std::make_shared<UniformBuffer>(lightBufferSize);
}
RenderManager::~RenderManager() {

}

void RenderManager::prepareVPData(const std::shared_ptr<RenderScene>& renderScene) {
	const std::shared_ptr<Camera>& camera = renderScene->main_camera;

	const glm::mat4& projection = camera->GetPerspective();
	const glm::mat4& view = camera->GetViewMatrix();
	//glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
	
	if (uniformVPBuffer) {
		uniformVPBuffer->bindBuffer();
		GLuint UBO = uniformVPBuffer->UBO;
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(projection));
		glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4),sizeof(glm::mat4), glm::value_ptr(view));
	}
	if (uniformVPBuffer->dirty) {
		//if dirty, set shader bindings of UBO
		for (std::shared_ptr<Shader>& shader : m_shader) {
			shader->setUniformBuffer("VP", uniformVPBuffer->binding);
		}
		uniformVPBuffer->setDirtyFlag(false);
	}
}

void RenderManager::preparePointLightData(const std::shared_ptr<RenderScene>& scene) {
	// TODO: test the consfusing offset
	// point light
	if (uniformPointLightBuffer->dirty) {
		uniformPointLightBuffer->dirty = false;
		uniformPointLightBuffer->bindBuffer();
		unsigned int UBO = uniformPointLightBuffer->UBO;
		int lightNum = scene->pointLights.size();
		int dataSize = 32; // data size for a single light (under std140 layout)
		for (int i = 0; i < lightNum; i++) {
			PointLightData& data = scene->pointLights[i]->data; 
			glBufferSubData(GL_UNIFORM_BUFFER, 
				0 + i * dataSize, 
				sizeof(glm::vec3), glm::value_ptr(data.color)); // ambient
			glBufferSubData(GL_UNIFORM_BUFFER, 
				16 + i * dataSize, 
				sizeof(glm::vec3), glm::value_ptr(data.position)); //
			/*glBufferSubData(GL_UNIFORM_BUFFER, 
				32 + i * dataSize, 
				sizeof(glm::vec3), glm::value_ptr(data.specular));
			glBufferSubData(GL_UNIFORM_BUFFER, 
				48 + i * dataSize, 
				sizeof(float), &data.constant);
			glBufferSubData(GL_UNIFORM_BUFFER, 
				52 + i * dataSize, 
				sizeof(float), &data.linear);
			glBufferSubData(GL_UNIFORM_BUFFER, 
				56 + i * dataSize, 
				sizeof(float), &data.quadratic);*/
		}
		// add the number of lights to UBO
		glBufferSubData(GL_UNIFORM_BUFFER,
			dataSize * lightNum, sizeof(int), &lightNum);
	}
	uniformPointLightBuffer->bindBuffer();
}

void RenderManager::prepareDirectionLightData(const std::shared_ptr<RenderScene>& scene) {
	// update light data only when dirty 
	if (uniformDirectionLightBuffer->dirty) {
		uniformDirectionLightBuffer->dirty = false;
		uniformDirectionLightBuffer->bindBuffer();
		unsigned int UBO = uniformDirectionLightBuffer->UBO;
		int lightNum = scene->directionLights.size();
		int dataSize = 48; // data size for a single light (under std140 layout)
		for (int i = 0; i < lightNum; i++) {
			DirectionLightData& data = scene->directionLights[i]->data; 
			glBufferSubData(GL_UNIFORM_BUFFER, 
				0 + i * dataSize, 
				sizeof(glm::vec3), glm::value_ptr(data.color)); // ambient
			glBufferSubData(GL_UNIFORM_BUFFER, 
				16 + i * dataSize, 
				sizeof(glm::vec3), glm::value_ptr(data.position)); //
			glBufferSubData(GL_UNIFORM_BUFFER, 
				32 + i * dataSize, 
				sizeof(glm::vec3), glm::value_ptr(data.direction));
			//glBufferSubData(GL_UNIFORM_BUFFER, 
				//48 + i * dataSize, 
				//sizeof(glm::vec3), glm::value_ptr(data.direction));
		}
		glBufferSubData(GL_UNIFORM_BUFFER, lightNum * dataSize, 
			sizeof(int), &lightNum);
	}
	uniformPointLightBuffer->bindBuffer();
}


void RenderManager::render(const std::shared_ptr<RenderScene>& scene) {
	// TODO: wrap up this function to be Scene rendering pass
	prepareVPData(scene);
	preparePointLightData(scene);
	prepareDirectionLightData(scene);

	for (auto object : scene->objects) {
		std::shared_ptr<MeshRenderer>& renderer = std::dynamic_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer) {
			renderer->shader->use();
			renderer->render();
		}
	}
	// render Terrain 
	std::shared_ptr<Terrain>& terrain = scene->terrain;
	if (terrain) {
		terrain->shader->use();
		terrain->render();
	}

	// render skybox 
	std::shared_ptr<SkyBox>& skybox = scene->skybox;
	if (skybox) {
		skybox->shader->use();
		skybox->render();
	}
}

std::shared_ptr<Shader> RenderManager::getShader(ShaderType type) {
	int index = static_cast<int>(type);
	if(!m_shader[index]){
		//if not initialized 
		m_shader[index] = RenderManager::generateShader(type);
	}
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
		case ShaderType::PBR:
			return std::make_shared<Shader>(
				"./src/shader/pbr.vs", "./src/shader/pbr.fs", nullptr,
				nullptr, nullptr
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
			return std::make_shared<Shader>(
				"./src/shader/terrain.vs", "./src/shader/pbr.fs", nullptr,
				"./src/shader/terrain.tesc", "./src/shader/terrain.tese"
				);
			break;
		default:
			std::cerr << "No such shader type" << '\n';
			break;
	}
	//return std::make_shared<Shader>(nullptr, nullptr, nullptr, nullptr, nullptr);
}
