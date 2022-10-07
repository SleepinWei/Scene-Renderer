#pragma once
#include<string>
#include<vector>
#include"../component/GameObject.h"
#include<memory>

class Material;
enum class ShaderType; 
class Shader; 
class SSBO;

class Terrain:public GameObject{
public:
	Terrain();
	~Terrain();
	std::shared_ptr<Terrain>loadHeightmap(const std::string& folder);
	std::shared_ptr<Terrain>loadHeightmap_(const std::string& folder);
	std::shared_ptr<Terrain> addShader(ShaderType st);
	std::shared_ptr<Terrain> addMaterial(std::shared_ptr<Material> material); // append 

	// render pass
	void render();
	void tessDrawCall();

	// compute Shader 
	void prepareData(); 
	void computeDrawCall(); // compute shader
	void renderCall();
private:
	void initVertexObject();

public:
	std::vector<float> vertices;
	std::vector<unsigned int> nodeIndex; // used in compute shader
	float yScale;
	float yShift;

	glm::mat4 model;
	std::shared_ptr<Material> material;
	std::shared_ptr<Material> terrainMaterial; 

	std::shared_ptr<Shader> shader;
	std::shared_ptr<Shader> computeShader;

	unsigned int VAO, VBO;
	unsigned int rez; // number of patches
	const int NUM_PATCH_PTS;

	// ssbo object that stores terrain data. 
	std::shared_ptr<SSBO> verticesSSBO; 
	std::shared_ptr<SSBO> indicesSSBO;
	std::shared_ptr<SSBO> inQueueSSBO;
	std::shared_ptr<SSBO> outQueueSSBO;
	std::shared_ptr<SSBO> indirectDrawSSBO;
	bool isIn; // inQueue  «∑Ò π”√ inQueueSSBO / or outQueueSSBO
};
