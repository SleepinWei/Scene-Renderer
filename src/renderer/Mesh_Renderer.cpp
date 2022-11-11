#include<glad/glad.h>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/glm.hpp>
#include<glm/gtx/transform2.hpp>
#include<glm/gtx/euler_angles.hpp>

#include"Mesh_Renderer.h"
#include"Material.h"
#include"Mesh_Filter.h"
#include"Texture.h"
#include"../utils/Shader.h"
#include <memory>
// #include<shader/Shader.h>
#include"../utils/Shader.h"
#include"../component/GameObject.h"
#include"../component/transform.h"
#include"../component/Lights.h"

//using namespace rttr;

//RTTR_REGISTRATION{
//	registration::class_<MeshRenderer>("MeshRenderer")
//	.constructor<>()(rttr::policy::ctor::as_std_shared_ptr);
//}

MeshRenderer::~MeshRenderer() {

}

std::shared_ptr<MeshRenderer> MeshRenderer::setShader(ShaderType type) {
	shader = renderManager->getShader(type);
	return shared_from_this();
}

std::shared_ptr<MeshRenderer> MeshRenderer::setMaterial(std::shared_ptr<Material> material) { 
	this->material = material; 
	return shared_from_this();
};

std::shared_ptr<MeshRenderer> MeshRenderer::setDrawMode(GLenum drawMode_) {
	drawMode = drawMode_;
	return shared_from_this();
}

std::shared_ptr<MeshRenderer> MeshRenderer::setPolyMode(GLenum ployMode_) {
	polyMode = ployMode_;
	return shared_from_this();
}

MeshRenderer::MeshRenderer():drawMode(GL_TRIANGLES),polyMode(GL_FILL) {
	name = "MeshRenderer";
	//VAO = 0;
	//VBO = 0;
	//EBO = 0;
}

/// <summary>
/// @param: useShader: determines whether the renderer
/// should use its shader. In Shadow Pass or G buffer pass
/// a global shader is used. 
/// </summary>
/// <param name="useShader"></param>
void MeshRenderer::render(bool useShader){
	const std::shared_ptr<Transform>& transform = std::dynamic_pointer_cast<Transform>(gameObject->GetComponent("Transform"));

	if (!transform) {
		return;
	}

	glm::mat4 trans = glm::translate(transform->position);
	auto rotation = transform->rotation;
	glm::mat4 eulerAngleYXZ = glm::eulerAngleYXZ(glm::radians(rotation.y), glm::radians(rotation.x), glm::radians(rotation.z));
	glm::mat4 scale = glm::scale(transform->scale); //缩放;
	glm::mat4 model = trans * scale * eulerAngleYXZ;
	//glm::mat4 mvp = projection * view * model;

	auto component_mesh_filter = gameObject->GetComponent("MeshFilter");
	auto mesh_filter = std::static_pointer_cast<MeshFilter>(component_mesh_filter);
	if (!mesh_filter) {
		return;
	}
	
	//std::shared_ptr<Shader> shader = material->shader; 
	for (auto& mesh : mesh_filter->meshes) {
		unsigned int& VAO = mesh->VAO;
		unsigned int& VBO = mesh->VBO;
		unsigned int& EBO = mesh->EBO;
		auto& vertices = mesh->vertices;
		auto& indices = mesh->indices;
		if (VAO == 0) {
			glGenBuffers(1, &VBO);
			glBindBuffer(GL_ARRAY_BUFFER, VBO);
			glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
				&(vertices[0]), GL_STATIC_DRAW);
			glGenBuffers(1, &EBO);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
				&(indices[0]), GL_STATIC_DRAW);

			glGenVertexArrays(1, &VAO);
			glBindVertexArray(VAO);
			{
				glBindBuffer(GL_ARRAY_BUFFER, VBO);
				glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(Vertex), (void*)offsetof(Vertex, Position));
				glVertexAttribPointer(1, 3, GL_FLOAT, false, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
				glVertexAttribPointer(2, 2, GL_FLOAT, false, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
				glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
				glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);
				glEnableVertexAttribArray(3);
				glEnableVertexAttribArray(4);

				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
			}
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		if (useShader && shader)
		{
			shader->use();
			//glEnable(GL_DEPTH_TEST);
			//glEnable(GL_CULL_FACE);
			shader->setMat4("model", model);

			// bind textures 
			if (material) {
				auto& textures = material->textures; // a hash map
				int texture_index = 0;
				for (auto iterator = textures.begin(); iterator != textures.end(); ++iterator) {
					//激活纹理单元0
					glActiveTexture(GL_TEXTURE0 + texture_index);
					//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
					glBindTexture(GL_TEXTURE_2D, iterator->second->id);
					//设置Shader程序从纹理单元0读取颜色数据
					shader->setInt((iterator->first).c_str(), texture_index);
					++texture_index;
				}
			}
		}

		glBindVertexArray(VAO);
		assert(VAO > 0);
		{
			if (polyMode == GL_LINE) {
				glPolygonMode(GL_FRONT_AND_BACK, polyMode);
			}
			glPatchParameteri(GL_PATCH_VERTICES, 3);
			// --- end debug
			glDrawElements(drawMode, indices.size(), GL_UNSIGNED_INT, 0);
			if (polyMode == GL_LINE) {
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			}
		}
	}
	glBindVertexArray(0);
}
