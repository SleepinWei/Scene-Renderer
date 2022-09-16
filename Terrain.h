#pragma once
#include<string>
#include<vector>
#include<glfw/glfw3.h>
#include<shader/Shader.h>
#include"Material.h"
#include"RenderManager.h"

extern class ResourceManager resourceManager;

class Terrain :public Renderable {
public:
	Terrain(TEX_TYPE tex1,TEX_TYPE tex2,ShaderType st);
	~Terrain();
	void initGeometry();
	void initVertexObject();
	virtual void render() override;
	virtual void registerShader(ShaderType st) override;

public:
	std::vector<float> vertices;
	float yScale;
	float yShift;
	//float xzScale;	
	glm::mat4 model;
	std::vector<std::shared_ptr<Material>> materials;

	GLuint VAO, VBO;
	unsigned int rez; // number of patches
	const int NUM_PATCH_PTS;
};
