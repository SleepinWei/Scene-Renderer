#pragma once
#include<string>
#include<vector>
#include<glfw/glfw3.h>
#include<shader/Shader.h>

class Terrain {
public:
	Terrain(const std::string& path,const std::string& normalPath);
	~Terrain();
	void initGeometry(const std::string& path,const std::string& normalPath);
	void initVertexObject();
	void render(Shader& shader);

public:
	std::vector<float> vertices;
	float yScale; 
	float yShift;
	//float xzScale;	
	glm::mat4 model;

	GLuint VAO, VBO;
	GLuint texture,normalTexture;
	unsigned int rez; // number of patches
	const int NUM_PATCH_PTS ; 
};
