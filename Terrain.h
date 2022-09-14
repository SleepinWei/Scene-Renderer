#pragma once
#include<string>
#include<vector>
#include<glfw/glfw3.h>
#include<shader/Shader.h>

class Terrain {
public:
	Terrain(const std::string& path);
	~Terrain();
	void initGeometry(const std::string& path);
	void initVertexObject();
	void render(Shader& shader);

public:
	std::vector<float> vertices;
	std::vector<unsigned int> indices;
	int NUM_STRIPS;
	int NUM_VERTS_PER_STRIP;
	float yScale; 
	float yShift;
	float xzScale;	

	GLuint VAO, VBO, EBO;
};
