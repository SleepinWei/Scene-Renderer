#include<glad/glad.h>
// #include<rttr/registration.h>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/glm.hpp>
#include<glm/gtx/transform2.hpp>
#include<glm/gtx/euler_angles.hpp>

#include"Mesh_Renderer.h"
#include"Material.h"
#include"Mesh_Filter.h"
#include"Texture.h"
#include<shader/Shader.h>
#include"../component/GameObject.h"
#include"../component/transform.h"

//using namespace rttr;

//RTTR_REGISTRATION{
//	registration::class_<MeshRenderer>("MeshRenderer")
//	.constructor<>()(rttr::policy::ctor::as_std_shared_ptr);
//}

MeshRenderer::~MeshRenderer() {

}

void MeshRenderer::setShader(ShaderType type) {
	shader = renderManager.getShader(type);
}

void MeshRenderer::setDrawMode(GLenum drawMode_) {
	drawMode = drawMode_;
}

MeshRenderer::MeshRenderer():drawMode(GL_TRIANGLES),polyMode(GL_FILL) {
	name = "MeshRenderer";
}

void MeshRenderer::render() {
	auto component_transform = gameObject->GetComponent("Transform");
	auto transform = std::dynamic_pointer_cast<Transform>(component_transform);
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
	auto mesh_filter = std::dynamic_pointer_cast<MeshFilter>(component_mesh_filter);
	if (!mesh_filter) {
		return;
	}
	
	//std::shared_ptr<Shader> shader = material->shader; 
	if (VAO == 0) {
		glGenBuffers(1, &VBO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER, mesh_filter->vertices.size() * sizeof(MeshFilter::Vertex),
			&(mesh_filter->vertices[0]), GL_STATIC_DRAW);
		glGenBuffers(1, &EBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh_filter->indices.size() * sizeof(unsigned int), 
			&(mesh_filter->indices[0]),GL_STATIC_DRAW);
		glGenVertexArrays(1, &VAO);
		
		glBindVertexArray(VAO); 
		{
			glBindBuffer(GL_ARRAY_BUFFER, VBO);
			glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(MeshFilter::Vertex), (void*)0);
			glVertexAttribPointer(1, 3, GL_FLOAT, false, sizeof(MeshFilter::Vertex), (void*)offsetof(MeshFilter::Vertex,Position));
			glVertexAttribPointer(2, 2, GL_FLOAT, false, sizeof(MeshFilter::Vertex), (void*)offsetof(MeshFilter::Vertex,TexCoords));
			glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(MeshFilter::Vertex), (void*)offsetof(MeshFilter::Vertex, Tangent));
			glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(MeshFilter::Vertex), (void*)offsetof(MeshFilter::Vertex, Bitangent));

			glEnableVertexAttribArray(0);
			glEnableVertexAttribArray(1);
			glEnableVertexAttribArray(2);
			glEnableVertexAttribArray(3);
			glEnableVertexAttribArray(4);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		}
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	shader->use(); 
	{
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		shader->setMat4("projection", projection);
		shader->setMat4("view", view);
		shader->setMat4("model", model);

		// bind textures 
		//TODO:
		std::vector<std::shared_ptr<Texture>> textures = material->textures;
		for (int texture_index = 0; texture_index < textures.size(); ++texture_index) {
			//激活纹理单元0
			glActiveTexture(GL_TEXTURE0 + texture_index);
			//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
			glBindTexture(GL_TEXTURE_2D, textures[texture_index]->id);
			//设置Shader程序从纹理单元0读取颜色数据
			shader->setInt(("material."+textures[texture_index]->type).c_str(), texture_index);
		}

		glBindVertexArray(VAO);
		{
			if (polyMode == GL_LINE) {
				glPolygonMode(GL_FRONT_AND_BACK, polyMode);
			}
			glDrawElements(drawMode, mesh_filter->indices.size(), GL_UNSIGNED_INT, 0);
			if (polyMode == GL_FILL) {
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			}
		}
		glBindVertexArray(0);
	}
}
