#pragma once

#include<glad/glad.h>
#include<glfw/glfw3.h>
#include<string>
#include<vector>
#include<shader/Shader.h>

class PBRMaterial {
public:
	PBRMaterial(std::string path);
	~PBRMaterial();

	GLuint albedoMap, normalMap, metallicMap, roughnessMap, aoMap;

	void bindTexture();
	void bindShader(Shader& shader);
	void destroy();
	void initTexture(std::string path);
};
