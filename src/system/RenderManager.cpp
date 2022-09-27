#include"RenderManager.h"
#include"../renderer/Mesh_Renderer.h"
#include"../object/Terrain.h"
#include"../object/SkyBox.h"
#include"../renderer/RenderScene.h"
#include"../buffer/UniformBuffer.h"
#include"../utils/Camera.h"
#include"../component/GameObject.h"
#include"../utils/Shader.h"

#include<glm/gtc/type_ptr.hpp>

RenderManager::RenderManager() {
	m_shader = std::vector<std::shared_ptr<Shader>>(ShaderTypeNum,nullptr);

	// VP buffer
	uniformVPBuffer = std::make_shared<UniformBuffer>(
			2 * sizeof(glm::mat4)
		);
	uniformVPBuffer->setBinding(0);
	shaderVPdirty = true; 
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
	if (shaderVPdirty) {
		//if dirty, set shader bindings of UBO
		for (std::shared_ptr<Shader>& shader : m_shader) {
			shader->setUniformBuffer("VP", uniformVPBuffer->binding);
		}
		shaderVPdirty = false; 
	}
}

void RenderManager::render(const std::shared_ptr<RenderScene>& renderScene) {
	for (auto object : renderScene->objects) {
		std::shared_ptr<MeshRenderer>& renderer = std::dynamic_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
		if (renderer) {
			renderer->shader->use();
			renderer->render();
		}
	}
	// render Terrain 
	std::shared_ptr<Terrain>& terrain = renderScene->terrain;
	if (terrain) {
		terrain->shader->use();
		terrain->render();
	}

	// render skybox 
	std::shared_ptr<SkyBox>& skybox = renderScene->skybox;
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
