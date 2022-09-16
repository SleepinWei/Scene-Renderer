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
class ResourceManager {
public:
	// textures are also renderables
	std::vector <std::shared_ptr<Material>> resource;
	// std::vector<int> resourceCnt; 

	std::shared_ptr<Material> registerResource(TEX_TYPE type);
	static std::shared_ptr<Material> generateResource(TEX_TYPE type);

public:
	const int maxlen = 10; 
	ResourceManager();
	~ResourceManager();
};

class Material :public Renderable {
public:
	int beginIndex;
	int materialCnt;
	void setBeginIndex(int index);
	int getMaterialCount()const;
};

class PBRMaterial:public Material{
private:
	void initTexture(const std::string& path);
public:
	PBRMaterial(const std::string& path);
	~PBRMaterial();
	GLuint albedoMap, normalMap, metallicMap, roughnessMap, aoMap;

	//void bindShader(Shader& shader);
	virtual void render() override;
	void destroy();
};

class HeightMaterial :public Material {
public:
	HeightMaterial(const std::string& path);
	~HeightMaterial();
	void initTexture(const std::string& path);
	virtual void render()override; 
	virtual void registerShader(ShaderType st)override;
public:
	GLuint texture, normalTexture;
};
