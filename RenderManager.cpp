#include"RenderManager.h"

RenderManager::RenderManager() {
	m_shader = std::vector<std::shared_ptr<Shader>>(ShaderTypeNum);
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
		m_shader[i]->setMat4("projection", projection);
		if (i == static_cast<int>(ShaderType::SKYBOX)) {
			m_shader[i]->setMat4("view", skyboxView);
		}
		else {
			m_shader[i]->setMat4("view", view);
		}
	}
}

void RenderManager::render(std::vector<std::shared_ptr<Renderable>>& objects) {
	for (auto object : objects) {
		object->shader->use();
		object->render();
	}
}

std::shared_ptr<Shader> RenderManager::getShader(ShaderType st) {
	return m_shader[static_cast<int>(st)];
}

std::shared_ptr<Shader> RenderManager::registerShader(ShaderType type) {
	int index = static_cast<int>(type);
	if(!m_shader[index]){
		//if not initialized 
		m_shader[index] = RenderManager::generateShader(type);
	}
	// m_shaderCnt[index] += 1; 
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
				"./shader/terrain.vs", "./shader/pbr.fs", nullptr,
				"./shader/terrain.tesc", "./shader/terrain.tese"
				);
			break;
		default:
			std::cerr << "No such shader type" << '\n';
			break;
	}
	return std::make_shared<Shader>(nullptr, nullptr, nullptr, nullptr, nullptr);
}
