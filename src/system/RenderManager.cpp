#include"RenderManager.h"
#include"../renderer/Mesh_Renderer.h"
#include"../object/Terrain.h"

RenderManager::RenderManager() {
	m_shader = std::vector<std::shared_ptr<Shader>>(ShaderTypeNum,nullptr);
	// m_shaderCnt = std::vector<int>(ShaderTypeNum);
}
RenderManager::~RenderManager() {

}

void RenderManager::updateShader(const Camera& camera) {
	glm::mat4 projection = camera.GetPerspective();
	glm::mat4 view = camera.GetViewMatrix();
	glm::mat4 skyboxView = glm::mat4(glm::mat3(view));

	for (int i = 0; i < m_shader.size(); i++) {
		if (m_shader[i] == nullptr)
			continue;
		m_shader[i]->use();
		m_shader[i]->setMat4("projection", projection);
		if (i == static_cast<int>(ShaderType::SKYBOX)) {
			m_shader[i]->setMat4("view", skyboxView);
		}
		else {
			m_shader[i]->setMat4("view", view);
		}
	}
}

void RenderManager::render(std::vector<std::shared_ptr<GameObject>>& objects,const glm::vec3& lightPos,const glm::vec3& lightColor) {
	for (auto object : objects) {
		if (object->name == "Terrain") {
			// how to improve this part? 
			auto t = std::dynamic_pointer_cast<Terrain>(object);
			t->shader->setVec3("light.Position", lightPos);
			t->shader->setVec3("light.Color", lightColor);
			t->render();
		}
		else {
			std::shared_ptr<MeshRenderer> renderer = std::dynamic_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
			renderer->shader->setVec3("light.Position", lightPos);
			renderer->shader->setVec3("light.Color", lightColor);
			renderer->render();
		}
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
				"./shader/light.vs", "./shader/light.fs", nullptr,
				nullptr, nullptr
				);
			break;
		case ShaderType::PBR:
			return std::make_shared<Shader>(
				"./shader/pbr.vs", "./shader/pbr.fs", nullptr,
				nullptr, nullptr
				);
			break;
		case ShaderType::SIMPLE:
			return std::make_shared<Shader>(
				"./shader/simple.vs", "./shader/simple.fs"
				);
			break;
		case ShaderType::SKYBOX:
			return std::make_shared<Shader>(
				"./shader/skybox.vs", "./shader/skybox.fs"
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
