#pragma once
#include<string>
#include<vector>
// #include<glfw/glfw3.h>
// #include<shader/Shader.h>
#include"../utils/Shader.h"
#include"../renderer/Material.h"
#include"../system/RenderManager.h"
#include"../component/GameObject.h"
#include<memory>

extern class ResourceManager resourceManager;

class Terrain:public GameObject{
public:
	Terrain();
	~Terrain();
	std::shared_ptr<Terrain>loadHeightmap(const std::string& folder);
	std::shared_ptr<Terrain> addShader(ShaderType st);
	std::shared_ptr<Terrain> addMaterial(std::shared_ptr<Material> material); // append 

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

	unsigned int VAO, VBO;
	unsigned int rez; // number of patches
	const int NUM_PATCH_PTS;
};
