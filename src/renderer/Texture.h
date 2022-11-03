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
	static std::shared_ptr<Texture> loadFromFile(const std::string& file_path);
	//std::shared_ptr<Texture> setType(const std::string& type);
	std::shared_ptr<Texture> genTexture(unsigned int DataType,unsigned int channelType,int width,int height);
	std::shared_ptr<Texture> genCubeMap(GLenum format, int width,int height);

public:
	int width, height;
	//std::string type; // type is now recorded in material.
	std::string name; // Î¨Ò»id£¬path
	unsigned int id;
};
