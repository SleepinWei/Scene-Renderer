#pragma once
#include<memory>
#include "engine/LogicAsset.h"
#include <vector>
#include<iostream>

//std::string pathToTexName(std::string file_path);
typedef unsigned int GLenum;
struct TextureSnapshot {
    int width=0,height=0,channels=0;
    unsigned int format=0,internalformat=0;
    std::string name;
    std::shared_ptr<const std::vector<unsigned char>> pixels;
    const unsigned char* data() const {return pixels && !pixels->empty()?pixels->data():nullptr;}
};
class Texture:public engine::LogicAsset,public std::enable_shared_from_this<Texture>{
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
    Texture(const Texture&)=delete;
    Texture& operator=(const Texture&)=delete;
    TextureSnapshot snapshot() const;
    void setPixels(int width,int height,int channels,std::vector<unsigned char>);
    void setStorageDescriptor(int,int,unsigned int,unsigned int);
    void freeze(); // Decoded cache entries are immutable and readable across threads.
    bool immutable() const {return immutable_;}
    uint64_t revision() const {checkRead();return engine::AssetIdentity::getContentRevision();}
    int getWidth() const {checkRead();return width;}
    int getHeight() const {checkRead();return height;}
    int getChannels() const {checkRead();return channels;}
    const std::string& getPath() const {checkRead();return name;}
    const unsigned char* pixels() const {checkRead();return data;}
    unsigned int gpuId() const;
    void swapGpuStorage(Texture&);
private:
    // Compatibility GPU code remains on its GL context thread. CPU snapshots
    // contain detached bytes, never these handles or mutable Texture pointers.
    friend class Material;
    friend class TerrainComponent;
    friend class SkyBox;
    friend class Sky;
    friend unsigned int ddsGL_load(const char*,std::shared_ptr<Texture>);
    void checkRead() const {if(!immutable_)checkLogicThread();}
    bool immutable_=false;
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
