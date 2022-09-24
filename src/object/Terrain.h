#pragma once
#include<string>
#include<vector>
#include<glfw/glfw3.h>
#include<shader/Shader.h>
#include"../renderer/Material.h"
#include"../system/RenderManager.h"
#include"../component/GameObject.h"

extern class ResourceManager resourceManager;

class Terrain:public GameObject {
public:
	Terrain();
	~Terrain();
	void loadHeightmap(const std::string& folder);
	void addShader(ShaderType st);
	void addMaterial(std::shared_ptr<Material> material); // append 

	void render();
private:
	void initVertexObject();

public:
	std::vector<float> vertices;
	float yScale;
	float yShift;

	glm::mat4 model;
	std::shared_ptr<Material> material;
	std::shared_ptr<Material> terrainMaterial; 

	std::shared_ptr<Shader> shader;

	GLuint VAO, VBO;
	unsigned int rez; // number of patches
	const int NUM_PATCH_PTS;
};
