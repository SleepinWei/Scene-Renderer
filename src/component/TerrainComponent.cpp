#include<glad/glad.h>
#include<stb/stb_image.h>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/type_ptr.hpp>
#include<fstream>
#include"component/TerrainComponent.h"
#include"utils/Utils.h"
#include"renderer/Texture.h"
#include"system/RenderManager.h"
#include"system/ResourceManager.h"
#include"renderer/Material.h"
#include"utils/Shader.h"
#include"buffer/SSBO.h"
#include"buffer/ImageTexture.h"
#include"component/Grass.h"
#include"component/GameObject.h"

/// <summary>
/// this is a deprecated function for loading heightmaps
/// this implementation is designed for tessellation shaders
/// </summary>
/// <param name="path"></param>
/// <returns></returns>
std::shared_ptr<TerrainComponent> TerrainComponent::loadHeightmap_(const std::string& path){
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
	terrainMaterial->addTexture(heightTex,"heightMap");
	terrainMaterial->addTexture(Texture::loadFromFile(path+"normalMap.png"),"normalMap");
	//ResourceManager::GetInstance().getResource();
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
	return std::dynamic_pointer_cast<TerrainComponent> (shared_from_this());
}

/// <summary>
/// load height map data 
/// In the current design, this function reads binary data from a heightmap.txt file
/// the data is stored in `terrainData` and released the first time Terrain::render() is called 
/// The terrainData will be passed to a texture buffer. Texture construction and data loading 
/// are seperated due to multi-thread concerns, which is a confusing and unnecessary problem in OPENGL
/// </summary>
/// <param name="path"></param>
/// <returns></returns>
std::shared_ptr<TerrainComponent> TerrainComponent::loadHeightmap(const std::string& path){
	int width, height, nrChannels; 
	//xzScale = 10.0f;
	//std::shared_ptr<Texture> heightTex = Texture::loadFromFileAsync(path + "heightMap.png",1); // read as gray scale
	//TODO:read directly from height.txt; 
	std::shared_ptr<Texture> heightTex = std::make_shared<Texture>();
	heightTex->width = 8192;
	heightTex->height = 8192;
	heightTex->internalformat = GL_R32F;
	heightTex->format = GL_RED;
	this->terrainMaterial->addTexture(heightTex, "heightMap");

	// load from txt
	std::string heightPath = path + "heightMap.txt";
	heightData = new float[heightTex->width * heightTex->height];
	std::ifstream fin(heightPath,std::ios::binary);
	//for (int i = 0; i < heightTex->width * heightTex->height; ++i) {
		//if (fin.peek() == EOF)
			//break;
		//fin >> heightData[i];
	//}
	fin.read((char*)heightData, heightTex->width * heightTex->height * sizeof(float));
	
	width = heightTex->width;
	height = heightTex->height;
	nrChannels = 1;
	//terrainMaterial->addTexture(heightTex,"heightMap");
	//terrainMaterial->addTexture(Texture::loadFromFile(path+"normalMap.png"),"normalMap");
	//ResourceManager::GetInstance().getResource();
	// indices
	yScale = 70.0f;
	yShift = -20.0f; 
	float xzScale = 200.0f;
	//float xzScale = 1.0f;
	model = glm::mat4(1);
	model = glm::translate(model, glm::vec3(0.0f, yShift,0.0f));
	model = glm::scale(model, glm::vec3(xzScale, yScale, xzScale));

	rez = 5; 

	// generate chunk vertex
	std::vector<unsigned int>(rez * rez * 2).swap(nodeIndex);
	for (unsigned i = 0; i < rez; i++)
	{
		for (unsigned j = 0; j < rez; j++)
		{
			int index = 2 * (i * rez + j);
			nodeIndex[index] = j;
			nodeIndex[index + 1] = i;
		}
	}

	return std::static_pointer_cast<TerrainComponent> (shared_from_this());
}

void TerrainComponent::initVertexObject() {
	if (!VAO) {
		glGenVertexArrays(1, &VAO);
		glBindVertexArray(VAO);
		
	}
}

TerrainComponent::~TerrainComponent() {
	glDeleteBuffers(1,&VBO);
	glDeleteVertexArrays(1,&VAO);
}

//std::shared_ptr<Terrain> Terrain::addShader(ShaderType st){
//	shader = renderManager->getShader(st);
//	return std::dynamic_pointer_cast<Terrain>(shared_from_this());
//}

std::shared_ptr<TerrainComponent>TerrainComponent::addMaterial(std::shared_ptr<Material> mat) {
	// push back materials
	this->material = mat;
	return  std::static_pointer_cast<TerrainComponent>(shared_from_this());
}

/// <summary>
/// this function is deprecated as current terrain surface construction abandons 
/// the tessellation style, rather a more custom and smart way using compute shaders
/// is used.
/// </summary>
void TerrainComponent::tessDrawCall(){
	/// <summary>
	/// this draw call uses tessellation shader and implements drawing through GL_PATCHES
	/// </summary>

	initVertexObject(); // actually works when first time called

	shader->setFloat("yShift", yShift);
	shader->setFloat("yScale", yScale);

	auto& textures = material->textures;
	//auto textures = material->textures;
	int texture_index = 0;
	for (auto iterator = textures.begin(); iterator!=textures.end(); ++iterator) {
		//激活纹理单元0
		glActiveTexture(GL_TEXTURE0 + texture_index);
		//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
		glBindTexture(GL_TEXTURE_2D, iterator->second->id);
		//设置Shader程序从纹理单元0读取颜色数据
		shader->setInt((iterator->first).c_str(), texture_index);
		++texture_index;
	}
	int beginIndex = textures.size();
	texture_index = 0;
	textures = terrainMaterial->textures;
	for (auto iterator = textures.begin(); iterator !=textures.end(); ++texture_index) {
		glActiveTexture(GL_TEXTURE0 + texture_index + beginIndex);
		//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
		glBindTexture(GL_TEXTURE_2D, iterator->second->id);
		//设置Shader程序从纹理单元0读取颜色数据
		shader->setInt((iterator->first).c_str(), texture_index+beginIndex);
		++texture_index;
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
	//std::cerr << "TerrainComponent Done" << "\n";
}

TerrainComponent::TerrainComponent() {
	// VAO
	Component::name = "TerrainComponent";
	VAO = 0, VBO = 0;
	rez = 5;
	initDone = false;

	material = std::make_shared<Material>();
	terrainMaterial = std::make_shared<Material>();

	shader = nullptr;
	terrainGBuffer = nullptr;
	computeShader = nullptr;
	complodMapShader = nullptr;
	compGenPatchShader = nullptr;

	patchesSSBO = nullptr;
	//std::shared_ptr<SSBO> indicesSSBO;
	outQueueSSBO = nullptr;
	indirectDrawSSBO = nullptr;
	finalNodeList = nullptr;
	nodeDescriptor = nullptr;
	lodMapTexture = nullptr;
}

/// <summary>
/// init() is called seperately from the constructor because opengl doesn't allow 
/// async operations on opengl state machine. Thus init() is only allowed in 
/// main render loop. 
/// </summary>
void TerrainComponent::init(){
	name = "TerrainComponent";
	polyMode = GL_FILL;

	// compute shader related buffers
	computeShader = std::make_shared<Shader>("./src/shader/terrain/terrain.comp");
	complodMapShader = std::make_shared<Shader>("./src/shader/terrain/lodMap.comp");
	compGenPatchShader = std::make_shared<Shader>("./src/shader/terrain/patch.comp");

	// ----
	patchesSSBO = std::make_shared<SSBO>(16 * 1024 * 1024); //16 MB
	//verticesSSBO->setBinding(1); 
	//patchesSSBO = std::make_shared<SSBO>(8192*8192);
	//indicesSSBO->setBinding(2);
	// ----
	inQueueSSBO = std::make_shared<SSBO>(4096);
	inQueueSSBO->setBinding(3);
	outQueueSSBO = std::make_shared<SSBO>(4096);
	outQueueSSBO->setBinding(4);
	finalNodeList = std::make_shared<SSBO>(8192 * 2);
	finalNodeList->setBinding(2);
	nodeDescriptor = std::make_shared<SSBO>(256 * 1024); //
	nodeDescriptor->setBinding(1);
	lodMapTexture = std::make_shared<ImageTexture>();
	lodMapTexture->genImageTexture(GL_RGBA32F, GL_RGBA, 160, 160);
	//--- 
	indirectDrawSSBO = std::make_shared<SSBO>(64);
	indirectDrawSSBO->setBinding(5);

	// shaders
	shader = RenderManager::GetInstance()->getShader(ShaderType::TERRAIN);
	shader->requireMat = true;
	terrainGBuffer = std::make_shared<Shader>("./src/shader/terrain/terrain.vs", "./src/shader/deferred/gBuffer.fs");
	terrainGBuffer->requireMat = true;

	
	//isIn = true;
}

/// <summary>
/// prepare data for computation & rendering
/// </summary>
void TerrainComponent::prepareData() {
	// set indirect draw SSBO
	if (!initDone) {
		init();

		//init heightMap
		if (!terrainMaterial->initDone) {
			terrainMaterial->initDone= true;
			auto& mat = terrainMaterial->textures;
			if (mat.find("heightMap") == mat.end()) {
				return;
			}
			auto& tex = mat["heightMap"];
			if (!tex->id) {
				glGenTextures(1, &tex->id);
				//glActiveTexture(GL_TEXTURE0);
				if (tex->id == 0) {
					std::cout << "Error: texture id is 0" << '\n';
				}
				glBindTexture(GL_TEXTURE_2D, tex->id); // all upcoming GL_TEXTURE_2D operations now have effect on this texture object
				// set the texture wrapping parameters
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	// set texture wrapping to GL_REPEAT (default wrapping method)
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
				// set texture filtering parameters
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				
				assert(heightData != nullptr);
				//assert(tex->channels == 1);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, tex->width, tex->height, 0, GL_RED, GL_FLOAT, heightData);
				glGenerateMipmap(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);

				// free data
				delete[] heightData;
				//stbi_image_free(tex->data);
				tex->data = nullptr;
			}
		}

		material->genTexture();
		initDone = true;
	}

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
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 16, nodeIndex.size() * sizeof(unsigned int), nodeIndex.data());

	outQueueSSBO->bindBuffer();
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 4, 4, &one);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8, 4, &one);

	// set finalNodeCnt
	finalNodeList->bindBuffer();
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 4, &zero);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 4, 4, &one);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8, 4, &one);

	// set render shader 
	//shader->use();
	//shader->setMat4("model", model);
	//shader->setInt("normalMap", 1);

	// set shader 
	computeShader->use();
	computeShader->setMat4("model", model);
	//computeShader->setFloat("yShift", yShift);
	//computeShader->setFloat("yScale", yScale);
	int beginIndex = 0;
	auto& textures = terrainMaterial->textures; // heightMap + normalMap
	int texture_index = 0;
	for (auto iterator = textures.begin(); iterator!=textures.end(); ++iterator) {
		glActiveTexture(GL_TEXTURE0 + texture_index + beginIndex);
		//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
		glBindTexture(GL_TEXTURE_2D, iterator->second->id);
		//设置Shader程序从纹理单元0读取颜色数据
		computeShader->use();
		computeShader->setInt((iterator->first).c_str(), texture_index+beginIndex);
		compGenPatchShader->use();
		compGenPatchShader->setInt((iterator->first).c_str(), texture_index + beginIndex);
		shader->use();
		shader->setInt((iterator->first).c_str(), texture_index + beginIndex);
		
		++texture_index;
	}

}

/// <summary>
/// compute view-dependent lod
/// </summary>
void TerrainComponent::compLodCall() {
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

/// <summary>
/// generate lod map (a texture) 
/// </summary>
void TerrainComponent::compLodMapCall() {
	// set binding
	lodMapTexture->setBinding(5);
	nodeDescriptor->setBinding(1);
	complodMapShader->use();

	glDispatchCompute(lodMapTexture->getWidth(), lodMapTexture->getHeight(), 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
}

/// <summary>
/// generate mesh grids
/// </summary>
void TerrainComponent::compGeneratePatchCall() {
	// set bindings
	finalNodeList->setBinding(2);
	patchesSSBO->setBinding(0);
	lodMapTexture->setBinding(5);
	indirectDrawSSBO->setBinding(5);
	// grass patches binding
	auto&& grassComponent = std::static_pointer_cast<Grass>(this->gameObject->GetComponent("Grass"));
	if (grassComponent) {
		grassComponent->grassPatchesBuffer->setBinding(1);
	}
	
	// for debug
	compGenPatchShader->use();
	//compGenPatchShader->setInt("heightMap", 0);
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

void TerrainComponent::renderCall(const std::shared_ptr<Shader>& outShader) {
	// bind patches
	this->patchesSSBO->setBinding(0);

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
	auto actualShader= (outShader == nullptr) ? shader : outShader;

	actualShader->use();
	actualShader->setMat4("model", model);
	// bind normal map
	//auto& normalTex = terrainMaterial->textures["normalMap"];
	//glActiveTexture(GL_TEXTURE1);
	//glBindTexture(GL_TEXTURE_2D, normalTex->id);
	//bind pbr textures
	int beginIndex = 2;  // heightMap, normalMap
	if (actualShader->requireMat==true && material) {
		auto& textures = material->textures;
		int texture_index = 0;
		for (auto iterator = textures.begin(); iterator != textures.end(); ++iterator) {
			//激活纹理单元0
			glActiveTexture(GL_TEXTURE0 + beginIndex + texture_index);
			//将加载的图片纹理句柄，绑定到纹理单元0的Texture2D上。
			glBindTexture(GL_TEXTURE_2D, iterator->second->id);
			//设置Shader程序从纹理单元0读取颜色数据
			actualShader->setInt((iterator->first).c_str(), beginIndex + texture_index);
			++texture_index;
		}
	}

	if (polyMode == GL_LINE) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	glCullFace(GL_FRONT);

	// draw
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectDrawSSBO->ssbo);
	glDrawArraysIndirect(GL_TRIANGLES, 0);

	

	glCullFace(GL_BACK);
	//glEnable(GL_CULL_FACE);
	if (polyMode == GL_LINE)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void TerrainComponent::constructCall() {
	//TODO: seperate prepareData() from compute Draw call
	//prepareData();
	compLodCall();
	compLodMapCall();
	compGeneratePatchCall();
}

/// <summary>
/// render terrain with .vs,.fs shader
/// </summary>
/// <param name="outShader">
/// outShader -> nullptr: use terrain->shader | terrainGbuffer : used when rendering gBuffer
/// </param>
void TerrainComponent::render(const std::shared_ptr<Shader>& outShader) {
	renderCall(outShader);
}

void TerrainComponent::setPolyMode(unsigned int polyMode_) {
	// GL_POLYGON_MODE : GL_FILL / GL_LINE
	this->polyMode = polyMode_;
}

void TerrainComponent::loadFromJson(json& data) {
	// load terrain data from json file
	if (data.find("heightMap") != data.end()) {
		std::string heightmap_path = data["heightMap"].get < std::string>();
		this->loadHeightmap(heightmap_path);
	}
	if (data.find("material") != data.end()) {
		json& mat = data["material"];
		for (auto iter = mat.begin(); iter != mat.end(); ++iter) {
			std::string mat_type = iter.key();
			std::string mat_path = iter.value().get < std::string>();
			this->material->addTextureAsync(mat_path, mat_type);
		}
	}
}
