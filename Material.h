#pragma once

#include<glad/glad.h>
#include<glfw/glfw3.h>
#include<string>
#include<vector>
#include<shader/Shader.h>
#include<memory>
#include<map>
#include"RenderManager.h"

enum class TEX_TYPE {
	RUST=0,
	GRASS,
	ROCK,
	METAL,
	SAND,
	HEIGHT
};
class Material;

class MaterialBufferBase {
public:
	// 
public:
	MaterialBufferBase():materialCnt(0){};
	int materialCnt;
	int getMaterialCount()const {
		return materialCnt;
	};
	virtual void render(std::shared_ptr<Shader> shader,int beginIndex) {};
};

class PBRMaterialBuffer :public MaterialBufferBase {
private:
	void destroy();
public:
	PBRMaterialBuffer(const std::string& path);
	virtual ~PBRMaterialBuffer();
	GLuint albedoMap, normalMap, metallicMap, roughnessMap, aoMap;
	virtual void render(std::shared_ptr<Shader> shader,int beginIndex) override;
};

class HeightMaterialBuffer :public MaterialBufferBase {
public:
	HeightMaterialBuffer(const std::string& path);
	virtual ~HeightMaterialBuffer();
	virtual void render(std::shared_ptr<Shader> shader, int beginIndex)override;
public:
	GLuint texture, normalTexture;
};

class ResourceManager {
public:
	// textures are also renderables
	std::vector <std::shared_ptr<MaterialBufferBase>> resource;
	// std::vector<int> resourceCnt; 

	std::shared_ptr<MaterialBufferBase> registerResource(TEX_TYPE type);
	static std::shared_ptr<MaterialBufferBase> generateResource(TEX_TYPE type);

public:
	const int maxlen = 10; 
	ResourceManager();
	~ResourceManager();
};
extern ResourceManager resourceManager;

class Material :public Renderable {
public:
	Material(TEX_TYPE type);
	void setBeginIndex(int index);
	virtual void render() override;
	virtual void registerShader(ShaderType st) override;
	int getMaterialCount();
public:
	int beginIndex;
	std::shared_ptr<MaterialBufferBase> materialBuffer; 
};


