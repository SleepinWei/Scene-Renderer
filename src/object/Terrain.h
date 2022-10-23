#pragma once
#include<string>
#include<vector>
#include"../component/GameObject.h"
#include<memory>

class Material;
enum class ShaderType; 
class Shader; 
class SSBO;
class ImageTexture;

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
	void setPolyMode(unsigned int polyMode_);
private:
	void initVertexObject();

public:
	std::vector<float> vertices;
	std::vector<unsigned int> nodeIndex; // used in compute shader
	std::vector<float> texCoords;
	float yScale;
	float yShift;
	unsigned int polyMode;

	glm::mat4 model;
	std::shared_ptr<Material> material;
	std::shared_ptr<Material> terrainMaterial; 

	std::shared_ptr<Shader> shader;
	std::shared_ptr<Shader> computeShader;
	std::shared_ptr<Shader> complodMapShader;
	std::shared_ptr<Shader> compGenPatchShader;

	unsigned int VAO, VBO;
	unsigned int rez; // number of patches
	const int NUM_PATCH_PTS = 4;

	// ssbo object that stores terrain data. 
	std::shared_ptr<SSBO> patchesSSBO; 
	//std::shared_ptr<SSBO> indicesSSBO;
	std::shared_ptr<SSBO> inQueueSSBO;
	std::shared_ptr<SSBO> outQueueSSBO;
	std::shared_ptr<SSBO> indirectDrawSSBO;
	std::shared_ptr<SSBO> finalNodeList;
	std::shared_ptr<SSBO> nodeDescriptor;
	std::shared_ptr<ImageTexture> lodMapTexture;
	bool isIn; // inQueue  «∑Ò π”√ inQueueSSBO / or outQueueSSBO
private: 
	void compLodCall();
	void compLodMapCall();
	void compGeneratePatchCall();
};
