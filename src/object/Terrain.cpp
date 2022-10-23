#include<glad/glad.h>
#include<stb/stb_image.h>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/type_ptr.hpp>
#include"Terrain.h"
#include"../utils/Utils.h"
#include"../renderer/Texture.h"
#include"../system/RenderManager.h"
#include"../renderer/ResourceManager.h"
#include"../renderer/Material.h"
#include"../utils/Shader.h"
#include"../buffer/SSBO.h"
#include"../buffer/ImageTexture.h"

extern std::unique_ptr<ResourceManager> resourceManager;
extern std::unique_ptr<RenderManager> renderManager;

std::shared_ptr<Terrain> Terrain::loadHeightmap_(const std::string& path){
	int width, height, nrChannels; 
	yScale = 1.0f;
	yShift = 0.0f; 
	//xzScale = 10.0f;
	model = glm::mat4(1);
	// set the texture wrapping parameters
	//unsigned char* data = stbi_load((path+"heightMap.png").c_str(), &width, &height, &nrChannels, 0);
	//stbi_image_free(data);
	std::shared_ptr<Texture>&& heightTex = Texture::loadFromFile(path + "heightMap.png");

	//FIX: because we need y-offset,we have to read the texture another time, though this is stupid.
	//stbi_set_flip_vertically_on_load(true);
	//unsigned char* data = stbi_load((path +"heightMap.png").c_str(), nullptr, nullptr,nullptr, 0);

	width = heightTex->width;
	height = heightTex->height;
	terrainMaterial->addTexture(heightTex);
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
			int textureIndex = i * heightTex->width + j * rez;
			vertices[index] = - width / 2.0f + width * i / (float)rez; // v.x
			//vertices[index + 1] = float(data[textureIndex]) / 255.0f * yScale - yShift; // v.y
			vertices[index + 1] = 0.0f;
			vertices[index + 2] = (-height / 2.0f + height * j / (float)rez); // v.z
			vertices[index + 3] = (i / (float)rez); // u
			vertices[index + 4] =  (j / (float)rez); // v

			vertices[index + 5] = (-width / 2.0f + width * (i + 1) / (float)rez); // v.x
			//vertices[index + 6] = float(data[textureIndex + heightTex->width]) / 255.0f * yScale - yShift; // v.y
			vertices[index + 6] = 0.0f;
			vertices[index + 7] = (-height / 2.0f + height * j / (float)rez); // v.z
			vertices[index+8] = ((i + 1) / (float)rez); // u
			vertices[index+9] = (j / (float)rez); // v

			vertices[index+10] = (-width / 2.0f + width * i / (float)rez); // v.x
			//vertices[index+11] = float(data[textureIndex + rez]) / 255.0f * yScale - yShift; // v.y
			vertices[index + 11] = 0.0f;
			vertices[index+12] = (-height / 2.0f + height * (j + 1) / (float)rez); // v.z
			vertices[index+13] = (i / (float)rez); // u
			vertices[index+14] = ((j + 1) / (float)rez); // v

			vertices[index+15] = (-width / 2.0f + width * (i + 1) / (float)rez); // v.x
			//vertices[index+16] = float(data[textureIndex + rez +heightTex->width]) / 255.0f * yScale - yShift; // v.y
			vertices[index + 16] = 0.0f;
			vertices[index+17] = (-height / 2.0f + height * (j + 1) / (float)rez); // v.z
			vertices[index+18] = ((i + 1) / (float)rez); // u
			vertices[index+19] = ((j + 1) / (float)rez); // v
		}
	}
	//std::cerr << "Vertices Done" << '\n';

	initVertexObject();

	//stbi_image_free(data);
	return std::dynamic_pointer_cast<Terrain> (shared_from_this());
}
std::shared_ptr<Terrain> Terrain::loadHeightmap(const std::string& path){
	int width, height, nrChannels; 
	yScale = 5.0f;
	yShift = -5.0f; 
	//xzScale = 10.0f;
	std::shared_ptr<Texture>&& heightTex = Texture::loadFromFile(path + "heightMap.png");
	width = heightTex->width;
	height = heightTex->height;
	terrainMaterial->addTexture(heightTex);
	terrainMaterial->addTexture(Texture::loadFromFile(path+"normalMap.png"));
	//resourceManager.getResource();
	// indices
	float xzScale = 100.0f;
	//float xzScale = 1.0f;
	model = glm::mat4(1);
	//model[0][0] = xzScale;//x scale
	//model[2][2] = xzScale; //z scale
	model = glm::translate(model, glm::vec3(0.0f, yShift,0.0f));
	model = glm::scale(model, glm::vec3(xzScale, yScale, xzScale));

	rez = 5; 

	// generate chunk vertex
	//std::vector<float>((rez + 1) * (rez + 1) * 4).swap(vertices);
	//std::vector<float>((rez + 1) * (rez + 1) * 2).swap(texCoords);
	std::vector<unsigned int>(rez * rez * 2).swap(nodeIndex);
	for (unsigned i = 0; i < rez; i++)
	{
		for (unsigned j = 0; j < rez; j++)
		{
			//int index = 4 * (i * (rez+1) + j);
			//vertices[index++] = - 1.0f + 2 * j / (float)rez; // v.x
			//vertices[index++] = 0.0f; // v.y
			//vertices[index++] = -1.0f + 2 * i / (float)rez; // v.z
			//vertices[index++] = 0.0f; // v.w
			//int index_ = 2 * (i * (rez + 1) + j);
			//texCoords[index_] = j / (float)rez; 
			//texCoords[index_ + 1] = i / (float)rez;
			int index = 2 * (i * rez + j);
			nodeIndex[index] = j;
			nodeIndex[index + 1] = i;
		}
	}

	// generate node indices
	/*std::vector<unsigned int>(rez * rez * 4).swap(nodeIndex);
	for (int i = 0; i < rez; i++) {
		for (int j = 0; j < rez; j++) {
			int index = 4 * (i * rez + j);
			unsigned int upper_left = i * (rez + 1) + j; 
			nodeIndex[index++] = upper_left; 
			nodeIndex[index++] = upper_left + 1;
			nodeIndex[index++] = upper_left + 1 + (1 + rez);
			nodeIndex[index++] = upper_left + (1 + rez);
		}
	}*/

	return std::dynamic_pointer_cast<Terrain> (shared_from_this());
}

void Terrain::initVertexObject() {
	if (!VAO) {
		glGenVertexArrays(1, &VAO);
		glBindVertexArray(VAO);
		glCheckError();
	}
}

Terrain::~Terrain() {
	glDeleteBuffers(1,&VBO);
	glDeleteVertexArrays(1,&VAO);
}

std::shared_ptr<Terrain> Terrain::addShader(ShaderType st){
	shader = renderManager->getShader(st);
	return std::dynamic_pointer_cast<Terrain>(shared_from_this());
}

std::shared_ptr<Terrain>Terrain::addMaterial(std::shared_ptr<Material> mat) {
	// push back materials
	this->material = mat;
	return  std::dynamic_pointer_cast<Terrain>(shared_from_this());
}

void Terrain::tessDrawCall(){
	/// <summary>
	/// this draw call uses tessellation shader and implements drawing through GL_PATCHES
	/// </summary>

	initVertexObject(); // actually works when first time called

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
	//glDisable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glDrawArrays(GL_PATCHES, 0, NUM_PATCH_PTS * rez * rez);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	//glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	//std::cerr << "Terrain Done" << "\n";
}

Terrain::Terrain(){
	material = std::make_shared<Material>();
	terrainMaterial = std::make_shared<Material>();
	name = "Terrain";
	polyMode = GL_FILL;

	// VAO
	VAO = 0, VBO = 0;
	rez = 5;

	// compute shader related buffers
	computeShader = std::make_shared<Shader>("./src/shader/terrain/terrain.comp");
	complodMapShader = std::make_shared<Shader>("./src/shader/terrain/lodMap.comp");
	compGenPatchShader = std::make_shared<Shader>("./src/shader/terrain/patch.comp");

	// ----
	patchesSSBO = std::make_shared<SSBO>(8192*8192);
	//verticesSSBO->setBinding(1); 
	//patchesSSBO = std::make_shared<SSBO>(8192*8192);
	//indicesSSBO->setBinding(2);
	// ----
	inQueueSSBO = std::make_shared<SSBO>(8192 * 4);
	inQueueSSBO->setBinding(3);
	outQueueSSBO = std::make_shared<SSBO>(8192 * 4);
	outQueueSSBO->setBinding(4);
	finalNodeList = std::make_shared<SSBO>(8525 * 8);
	finalNodeList->setBinding(2);
	nodeDescriptor = std::make_shared<SSBO>(8192 * 8192);
	nodeDescriptor->setBinding(1);
	lodMapTexture = std::make_shared<ImageTexture>();
	lodMapTexture->genImageTexture(GL_RGBA32F, GL_RGBA, 160, 160);
	//--- 
	indirectDrawSSBO = std::make_shared<SSBO>(64);
	indirectDrawSSBO->setBinding(5);
	glCheckError();
	isIn = true;
}

void Terrain::prepareData() {
	// set indirect draw SSBO
	indirectDrawSSBO->bindBuffer();

	unsigned int zero = 0; 
	if (indirectDrawSSBO->dirty) {
		unsigned int one = 1; 
		unsigned int count = 6;
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 4, &count);//count 
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8, 4, &zero); // firstIndex
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 12, 4, &zero); // baseVertex
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 16, 4, &zero); // baseInstance
		//glBufferSubData(GL_SHADER_STORAGE_BUFFER, 20, 4, &zero);
		indirectDrawSSBO->dirty = false;
	}
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 4, 4, &zero); // primCount
	//unsigned int vertexCnt = (rez + 1) * (rez + 1);
	//glBufferSubData(GL_SHADER_STORAGE_BUFFER, 20, 4, &vertexCnt);

	inQueueSSBO->bindBuffer();
	unsigned int nodeCnt = rez * rez;
	unsigned int one = 1;
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 4, &nodeCnt);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 4, 4, &one);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8, 4, &one);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 16, nodeIndex.size() * sizeof(unsigned int), &nodeIndex[0]);

	outQueueSSBO->bindBuffer();
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 4, 4, &one);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8, 4, &one);

	// set finalNodeCnt
	finalNodeList->bindBuffer();
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 4, &zero);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 4, 4, &one);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8, 4, &one);

	// set render shader 
	shader->use();
	shader->setMat4("model", model);
	shader->setInt("normalMap", 1);

	// set shader 
	computeShader->use();
	computeShader->setMat4("model", model);
	//computeShader->setFloat("yShift", yShift);
	//computeShader->setFloat("yScale", yScale);
	int beginIndex = 0;
	auto& textures = terrainMaterial->textures; // heightMap + normalMap
	for (int texture_index = 0; texture_index < textures.size(); texture_index++) {
		glActiveTexture(GL_TEXTURE0 + texture_index + beginIndex);
		//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
		glBindTexture(GL_TEXTURE_2D, textures[texture_index]->id);
		//设置Shader程序从纹理单元0读取颜色数据
		computeShader->setInt((textures[texture_index]->type).c_str(), texture_index+beginIndex);
	}

}

void Terrain::computeDrawCall() {
	compLodCall();
	compLodMapCall();
	compGeneratePatchCall();
}

void Terrain::compLodCall() {
	// set binding 
	finalNodeList->setBinding(2);
	nodeDescriptor->setBinding(1);
	// binding set end----
	const int MAX_LOD = 5; 
	for (unsigned int i = 0; i <= MAX_LOD; i++) {
		// 循环计算 LOD
		inQueueSSBO->setBinding(3);
		outQueueSSBO->setBinding(4);
		outQueueSSBO->bindBuffer();

		unsigned int zero = 0;
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 4, &zero); // 设置 out buffer 的 nodeCnt 为 0

		computeShader->use();
		computeShader->setUInt("currentLOD", i);
		inQueueSSBO->bindBuffer();

		glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, inQueueSSBO->ssbo);
		glDispatchComputeIndirect(0);
		//glDispatchCompute(nodeCnt, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
		
		//isIn = !isIn;
		std::swap(inQueueSSBO->ssbo, outQueueSSBO->ssbo); // 交换buffer
	}
}

void Terrain::compLodMapCall() {
	// set binding
	lodMapTexture->setBinding(5);
	nodeDescriptor->setBinding(1);
	complodMapShader->use();

	glDispatchCompute(lodMapTexture->getWidth(), lodMapTexture->getHeight(), 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
}

void Terrain::compGeneratePatchCall() {
	//TODO;
	// set bindings
	finalNodeList->setBinding(2);
	patchesSSBO->setBinding(0);
	lodMapTexture->setBinding(5);
	indirectDrawSSBO->setBinding(5);
	
	// for debug
	compGenPatchShader->use();
	compGenPatchShader->setInt("heightMap", 0);
	glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, finalNodeList->ssbo);
	glDispatchComputeIndirect(0);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |GL_COMMAND_BARRIER_BIT);

	//freopen("./output.txt", "w",stdout);
	//patchesSSBO->bindBuffer();
	//float* data = (float*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
	//for (int i = 0; i < 1000; i++) {
	//	for (int j = 0; j < 24; j++) {
	//		std::cout << data[24 * i + j] << ' ';
	//	}
	//	std::cout << '\n';
	//}
	//glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
	// end for 
}

void Terrain::renderCall() {
	// test renderCall
	initVertexObject();

	glBindVertexArray(VAO);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	
	//EBO
	//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indicesSSBO->ssbo);

	// VBO
	//glBindBuffer(GL_ARRAY_BUFFER, verticesSSBO->ssbo);

	//glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 0);
	//glEnableVertexAttribArray(0);
	//glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)16);
	//glEnableVertexAttribArray(1);

	shader->use();

	// bind normal map
	auto& normalTex = terrainMaterial->textures[1];
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, normalTex->id);
	//bind pbr textures
	int beginIndex = 2;  // heightMap, normalMap
	if (material) {
		std::vector<std::shared_ptr<Texture>>& textures = material->textures;
		for (int texture_index = 0; texture_index < textures.size(); ++texture_index) {
			//激活纹理单元0
			glActiveTexture(GL_TEXTURE0 + beginIndex + texture_index);
			//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
			glBindTexture(GL_TEXTURE_2D, textures[texture_index]->id);
			//设置Shader程序从纹理单元0读取颜色数据
			shader->setInt(("material." + textures[texture_index]->type).c_str(), beginIndex + texture_index);
		}
	}

	if (polyMode == GL_LINE) {
		glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
	}
	glCullFace(GL_FRONT);

	// draw
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectDrawSSBO->ssbo);
	glDrawArraysIndirect(GL_TRIANGLES, 0);

	glCheckError();

	glCullFace(GL_BACK);
	//glEnable(GL_CULL_FACE);
	if(polyMode == GL_LINE)
		glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
}

void Terrain::render() {
	isIn = true; // reset

	prepareData(); 
	computeDrawCall();
	renderCall();
}

void Terrain::setPolyMode(unsigned int polyMode_) {
	this->polyMode = polyMode_;
}
