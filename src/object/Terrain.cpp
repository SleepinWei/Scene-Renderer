#include<glad/glad.h>
#include<stb/stb_image.h>
#include<glm/gtc/matrix_transform.hpp>
#include"Terrain.h"
#include"../utils/Utils.h"
#include"../renderer/Texture.h"
#include"../renderer/ResourceManager.h"
extern class ResourceManager resourceManager;

std::shared_ptr<Terrain> Terrain::loadHeightmap(const std::string& path){
	int width, height, nrChannels; 
	yScale = 1.0f;
	yShift = 1.0f; 
	//xzScale = 10.0f;
	model = glm::mat4(1);
	// set the texture wrapping parameters
	unsigned char* data = stbi_load((path+"heightMap.png").c_str(), &width, &height, &nrChannels, 0);
	stbi_image_free(data);
	terrainMaterial->addTexture(Texture::loadFromFile(path + "heightMap.png"));
	terrainMaterial->addTexture(Texture::loadFromFile(path+"normalMap.png"));
	//resourceManager.getResource();
	// indices
	float xzScale = 20.0f/std::max(width,height);
	//float xzScale = 1.0f;
	model[0][0] = xzScale;//x scale
	model[2][2] = xzScale; //z scale

	rez = 40;
	vertices = std::vector<float>(20 * rez * rez, 0);
	for (unsigned i = 0; i <= rez - 1; i++)
	{
		for (unsigned j = 0; j <= rez - 1; j++)
		{
			int index = 20 * (i * rez + j);
			vertices[index] = - width / 2.0f + width * i / (float)rez; // v.x
			vertices[index+1] = 0.0f; // v.y
			vertices[index + 2] = (-height / 2.0f + height * j / (float)rez); // v.z
			vertices[index + 3] = (i / (float)rez); // u
			vertices[index + 4] =  (j / (float)rez); // v

			vertices[index + 5] = (-width / 2.0f + width * (i + 1) / (float)rez); // v.x
			vertices[index + 6] = (0.0f); // v.y
			vertices[index + 7] = (-height / 2.0f + height * j / (float)rez); // v.z
			vertices[index+8] = ((i + 1) / (float)rez); // u
			vertices[index+9] = (j / (float)rez); // v

			vertices[index+10] = (-width / 2.0f + width * i / (float)rez); // v.x
			vertices[index+11] = (0.0f); // v.y
			vertices[index+12] = (-height / 2.0f + height * (j + 1) / (float)rez); // v.z
			vertices[index+13] = (i / (float)rez); // u
			vertices[index+14] = ((j + 1) / (float)rez); // v

			vertices[index+15] = (-width / 2.0f + width * (i + 1) / (float)rez); // v.x
			vertices[index+16] = (0.0f); // v.y
			vertices[index+17] = (-height / 2.0f + height * (j + 1) / (float)rez); // v.z
			vertices[index+18] = ((i + 1) / (float)rez); // u
			vertices[index+19] = ((j + 1) / (float)rez); // v
		}
	}
	//std::cerr << "Vertices Done" << '\n';

	initVertexObject();
	return std::dynamic_pointer_cast<Terrain> (shared_from_this());
}

void Terrain::initVertexObject() {
	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

	//position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	//std::cerr << "Done1" << '\n';
	glPatchParameteri(GL_PATCH_VERTICES, NUM_PATCH_PTS);
}

Terrain::~Terrain() {
	glDeleteBuffers(1,&VBO);
	glDeleteVertexArrays(1,&VAO);
}
std::shared_ptr<Terrain> Terrain::addShader(ShaderType st){
	shader = renderManager.getShader(st);
	return std::dynamic_pointer_cast<Terrain>(shared_from_this());
}

std::shared_ptr<Terrain>Terrain::addMaterial(std::shared_ptr<Material> mat) {
	// push back materials
	this->material = mat;
	return  std::dynamic_pointer_cast<Terrain>(shared_from_this());
}

void Terrain::render() {
	shader->setFloat("yShift", yShift);
	shader->setFloat("yScale", yScale);

	std::vector<std::shared_ptr<Texture>> textures = material->textures;
	//auto textures = material->textures;
	for (int texture_index = 0; texture_index < textures.size(); ++texture_index) {
		//激活纹理单元0
		glActiveTexture(GL_TEXTURE0 + texture_index);
		//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
		glBindTexture(GL_TEXTURE_2D, textures[texture_index]->id);
		//设置Shader程序从纹理单元0读取颜色数据
		shader->setInt(("material."+textures[texture_index]->type).c_str(), texture_index);
	}
	int beginIndex = textures.size();
	textures = terrainMaterial->textures;
	for (int texture_index = 0; texture_index < textures.size(); texture_index++) {
		glActiveTexture(GL_TEXTURE0 + texture_index + beginIndex);
		//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
		glBindTexture(GL_TEXTURE_2D, textures[texture_index]->id);
		//设置Shader程序从纹理单元0读取颜色数据
		shader->setInt((textures[texture_index]->type).c_str(), texture_index+beginIndex);
	}

	shader->setMat4("model", model);

	//std::cout << VAO << '\n';
	glBindVertexArray(VAO);
	glDisable(GL_CULL_FACE);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glDrawArrays(GL_PATCHES, 0, NUM_PATCH_PTS * rez * rez);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_CULL_FACE);
	//std::cerr << "Terrain Done" << "\n";
}

Terrain::Terrain():NUM_PATCH_PTS(4) {
	material = std::make_shared<Material>();
	terrainMaterial = std::make_shared<Material>();
	name = "Terrain";
}
