#pragma once
#include<memory>
#include<iostream>
#include<stb/stb_image.h>

//std::string pathToTexName(std::string file_path);
typedef unsigned int GLenum;
class Texture:public std::enable_shared_from_this<Texture>{
public:
	Texture();
	~Texture();
public:
	static std::shared_ptr<Texture> loadFromFile(const std::string& file_path,int desired_channels=0);
	static std::shared_ptr<Texture> loadFromFileAsync(const std::string& filename,int desired_channels=0);
	//std::shared_ptr<Texture> setType(const std::string& type);
	std::shared_ptr<Texture> genTexture(unsigned int DataType,unsigned int channelType,int width,int height);
	std::shared_ptr<Texture> genTextureAsync(unsigned int DataType, unsigned int channelType, int width, int height);
	std::shared_ptr<Texture> genCubeMap(GLenum format, int width,int height);
	//generate a arrray texture for CSM
	std::shared_ptr<Texture> genTextureArray(GLenum internalformat, GLenum format, GLenum type, int width, int height, int mipmap_level,int layers);

	void bind(unsigned int target, int binding);

public:
	int width, height,channels;
	//std::string type; // type is now recorded in material.
	std::string name; // Î¨Ò»id£¬path
	unsigned int id;
	//unsigned int pbo;
	unsigned char* data;
	unsigned int internalformat;
	unsigned int format;
	int num_mipmaps;
};
