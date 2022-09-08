#include"Material.h"
#include"src/Utils.h"
#include<stb/stb_image.h>

void PBRMaterial::initTexture(std::string path) {
	albedoMap = loadTexture((path + "albedo.png").c_str());
	normalMap = loadTexture((path + "normal.png").c_str());
	metallicMap = loadTexture((path + "metallic.png").c_str());
	roughnessMap = loadTexture((path + "roughness.png").c_str());
	aoMap = loadTexture((path + "ao.png").c_str());
}

PBRMaterial::PBRMaterial(std::string path) {
	initTexture(path);
}

void PBRMaterial::bindTexture() {
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, albedoMap);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, normalMap);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, metallicMap);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, roughnessMap);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, aoMap);
}

void PBRMaterial::bindShader(Shader& shader) {
	shader.setInt("material.albedo", 0);
	shader.setInt("material.normal", 1);
	shader.setInt("material.metallic", 2);
	shader.setInt("material.roughness", 3);
	shader.setInt("material.ao", 4);
}

void PBRMaterial::destroy() {
	glDeleteTextures(1, &albedoMap);
	glDeleteTextures(1, &normalMap);
	glDeleteTextures(1, &metallicMap);
	glDeleteTextures(1, &roughnessMap);
	glDeleteTextures(1, &aoMap);
}

PBRMaterial::~PBRMaterial() {
	destroy();
}
