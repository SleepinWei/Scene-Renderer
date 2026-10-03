#include "system/RenderManager.h"
#include<glad/glad.h>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/glm.hpp>
#include<glm/gtx/transform2.hpp>
#include<glm/gtx/euler_angles.hpp>

#include"component/Mesh_Renderer.h"
#include"renderer/Material.h"
#include"component/Mesh_Filter.h"
#include"renderer/Texture.h"
#include"utils/Shader.h"
#include"utils/Utils.h"
#include <memory>
// #include<shader/Shader.h>
#include"utils/Shader.h"
#include"component/GameObject.h"
#include"component/transform.h"
#include"component/Lights.h"

MeshRenderer::~MeshRenderer() {

}

std::shared_ptr<MeshRenderer> MeshRenderer::setLegacyShader(std::shared_ptr<Shader> value) {
    checkLogicThread();
    if (!value) throw std::invalid_argument("Custom shader cannot be null");
    shader = std::move(value);
    invalidate();
    return shared_from_this();
}

std::shared_ptr<MeshRenderer> MeshRenderer::setShader(ShaderType type) {
    checkLogicThread();
	if(int(type)<0 || type>=ShaderType::KIND_COUNT)throw std::invalid_argument("Invalid shader type");
    auto selected=RenderManager::GetInstance()->getShader(type);shaderType=type;shader=std::move(selected);invalidate();
	return shared_from_this();
}

std::shared_ptr<MeshRenderer> MeshRenderer::setShader(std::string type) {
    checkLogicThread();
	ShaderType shadertype = ShaderType::SIMPLE;
	if(type=="simple")shadertype=ShaderType::SIMPLE;
    else if(type=="hdr")shadertype=ShaderType::HDR;
    else if(type=="depth")shadertype=ShaderType::DEPTH;
    else if (type == "pbr") {
		shadertype = ShaderType::PBR;
	}
	else if (type == "pbr_tess") {
		shadertype = ShaderType::PBR_TESS;
	}
	else if (type == "pbr_clearcoat") {
		shadertype = ShaderType::PBR_CLEARCOAT;
	}
	else if (type == "pbr_anisotropy") {
		shadertype = ShaderType::PBR_ANISOTROPY;
	}
	else if (type == "pbr_sss") {
		shadertype = ShaderType::PBR_SSS;
	}
	else if (type == "light") {
		shadertype = ShaderType::LIGHT;
	}
	else if (type == "terrain") {
		shadertype = ShaderType::TERRAIN;
	}
	else if (type == "sky") {
		shadertype = ShaderType::SKY;
	}
	else if (type == "test") {
		shadertype = ShaderType::TEST;
	}
	else if (type == "skybox") {
		shadertype = ShaderType::SKYBOX;
	}
	else throw std::invalid_argument("Unknown shader name: "+type);

	return setShader(shadertype);
}


// std::shared_ptr<MeshRenderer> MeshRenderer::setMaterial(std::shared_ptr<Material> material) { 
// 	this->material = material; 
// 	return shared_from_this();
// };

std::shared_ptr<MeshRenderer> MeshRenderer::setDrawMode(GLenum drawMode_) {
    checkLogicThread();
if(drawMode_!=GL_TRIANGLES && drawMode_!=GL_PATCHES && drawMode_!=GL_POINTS && drawMode_!=GL_LINES)throw std::invalid_argument("Invalid draw mode");
	drawMode = drawMode_;invalidate();
	return shared_from_this();
}

std::shared_ptr<MeshRenderer> MeshRenderer::setPolyMode(GLenum ployMode_) {
    checkLogicThread();
if(ployMode_!=GL_FILL && ployMode_!=GL_LINE && ployMode_!=GL_POINT)throw std::invalid_argument("Invalid polygon mode");
	polyMode = ployMode_;invalidate();
	return shared_from_this();
}

MeshRenderer::MeshRenderer():drawMode(GL_TRIANGLES),polyMode(GL_FILL) {
	name = "MeshRenderer";
	// material = std::make_shared<Material>();
	//VAO = 0;
	//VBO = 0;
	//EBO = 0;
}


/// <summary>
/// @param: useShader: determines whether the renderer
/// should use its own shader. In Shadow Pass or G buffer pass
/// a global shader is given. 
/// </summary>
/// <param name="useShader"></param>
void MeshRenderer::render(const std::shared_ptr<Shader>& outShader){
    checkLogicThread();
	glCheckError();
	const std::shared_ptr<Transform>& transform = std::static_pointer_cast<Transform>(owner()->GetComponent("Transform"));

	if (!transform) {
		return;
	}

	glm::mat4 trans = glm::translate(transform->getPosition());
	auto rotation = transform->getRotation();
	glm::mat4 eulerAngleYXZ = glm::eulerAngleYXZ(glm::radians(rotation.y), glm::radians(rotation.x), glm::radians(rotation.z));
	glm::mat4 scale = glm::scale(transform->getScale()); //缩放;
	glm::mat4 model = trans * scale * eulerAngleYXZ;
	//glm::mat4 mvp = projection * view * model;

	auto component_mesh_filter = owner()->GetComponent("MeshFilter");
	auto mesh_filter = std::static_pointer_cast<MeshFilter>(component_mesh_filter);
	if (!mesh_filter) {
		return;
	}

	auto actualShader = (outShader == nullptr) ? shader : outShader;

	for (auto& mesh : mesh_filter->getMeshes()) {
		mesh->genVAO();
        const auto VAO = mesh->getVAO();

		const auto EBO = mesh->getEBO();
		auto& vertices = mesh->getVertices();
		auto& indices = mesh->getIndices();
		auto material = mesh->getMaterial();

		glCheckError();

		actualShader->use();
		glCheckError();

		actualShader->setMat4("model", model);
		actualShader->setFloat("alphaCutoff", material ? material->getAlphaCutoff() : 0.0f);
		actualShader->setVec3("albedoFactor", material ? material->getAlbedoFactor() : glm::vec3(1.0f));
		if (material && material->getAlphaCutoff() > 0.0f) {
			material->genTexture();
			glActiveTexture(GL_TEXTURE31);
			glBindTexture(GL_TEXTURE_2D, material->getTextures().at("material.albedo")->id);
			actualShader->setInt("alphaTexture", 31);
		}

		if (actualShader->requireMat==true && material)
		{
			material->genTexture();
			glCheckError();
			//glEnable(GL_DEPTH_TEST);
			//glEnable(GL_CULL_FACE);

			// bind textures 
			if (material) {
				auto& textures = material->getTextures(); // a hash map
				int texture_index = 0;
				for (auto iterator = textures.begin(); iterator != textures.end(); ++iterator) {
					//激活纹理单元0
					glActiveTexture(GL_TEXTURE0 + texture_index);
					//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
					glBindTexture(GL_TEXTURE_2D, iterator->second->id);
					//设置Shader程序从纹理单元0读取颜色数据
					actualShader->setInt((iterator->first).c_str(), texture_index);
					++texture_index;
				}
				glCheckError();

				if (material->getHasSubSurface()) {
					// 
				}
			}
		}

		
		if (material && material->getTwoSided()) glDisable(GL_CULL_FACE);
		glBindVertexArray(VAO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		assert(VAO > 0);
		{
			if (polyMode == GL_LINE) {
				glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			}
			if(drawMode == GL_PATCHES)
				glPatchParameteri(GL_PATCH_VERTICES, 3);
			// --- end debug
			#ifdef SCENERENDERER_METAL
            const auto actualMode=(outShader && outShader!=shader && drawMode==GL_PATCHES)?GL_TRIANGLES:drawMode;
            glDrawElements(actualMode, indices.size(), GL_UNSIGNED_INT, 0);
#else
            glDrawElements(drawMode, indices.size(), GL_UNSIGNED_INT, 0);
#endif
			glCheckError();

			if (material && material->getTwoSided()) glEnable(GL_CULL_FACE);
			if (polyMode == GL_LINE) {
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			}
			glCheckError();
		}
	}
	
	glBindVertexArray(0);
}

void MeshRenderer::loadFromJson(json& data) {
    checkLogicThread();
	{
		// if (data.find("material") != data.end()) {
			// this->material->loadFromJson(data["material"]);
		// }
	}
	{
		auto& shader = data["shader"];
		std::string shader_type = shader.get<std::string>();
		this->setShader(shader_type);
	}
	{
		if (data.find("drawmode") != data.end()) {
			std::string s_drawmode = data["drawmode"].get<std::string>();
			unsigned int drawMode_ = 0;
			if(s_drawmode == "triangles"){
				drawMode_ = GL_TRIANGLES;
			}
			else if (s_drawmode == "patches") {
				drawMode_ = GL_PATCHES;
			}
			else if (s_drawmode == "point") {
				drawMode_ = GL_POINTS;
			}
			this->setDrawMode(drawMode_);
		}
	}
}
