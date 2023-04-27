#pragma once
#include<string>
#include<vector>
#include"../component/Component.h"
#include<memory>

class Material;
enum class ShaderType; 
class Shader; 
class SSBO;
class ImageTexture;
class Component;

class TerrainComponent:public Component,public std::enable_shared_from_this<TerrainComponent>{
public:
	TerrainComponent();
	~TerrainComponent();
	void init();
	std::shared_ptr<TerrainComponent>loadHeightmap(const std::string& folder);
	std::shared_ptr<TerrainComponent>loadHeightmap_(const std::string& folder);
	//std::shared_ptr<Terrain> addShader(ShaderType st);
	std::shared_ptr<TerrainComponent> addMaterial(std::shared_ptr<Material> material); // append 

	virtual void loadFromJson(json& data) override;
	// render pass
	void render(const std::shared_ptr<Shader>& outShader);

	// compute Shader 
	void constructCall();
	void prepareData(); 

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
	bool initDone;

	glm::mat4 model;
	std::shared_ptr<Material> material;
	std::shared_ptr<Material> terrainMaterial; 
	float* heightData;

	std::shared_ptr<Shader> shader;
	std::shared_ptr<Shader> terrainGBuffer; 

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
	//bool isIn; // inQueue  «∑Ò π”√ inQueueSSBO / or outQueueSSBO
private: 
	void tessDrawCall();
	void compLodCall();
	void compLodMapCall();
	void compGeneratePatchCall();
	void renderCall(const std::shared_ptr<Shader>& outShader);
};

