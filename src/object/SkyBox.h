#pragma once
#include<glad/glad.h>
#include<glfw/glfw3.h>
#include<string>
#include<vector>
#include<shader/Shader.h>

unsigned int loadCubemap(std::vector<std::string>& faces);


class SkyBox {
public:
	GLuint VAO, VBO, texture;
	std::vector<std::string> faces;

	SkyBox(const std::string path);
	~SkyBox();
	void render(Shader& shader);
};
