#pragma once

#include"../component/Component.h"
#include<string>
#include<memory>
#include<glm/glm.hpp>
#include<vector>
#include"../utils/header.h"
#include"../utils/BoundingBox.h"

class Material;
class Texture; 

const int MAX_BONE_INFLUENCE = 4;
//const double PI = 3.1415926; 

enum class SHAPE {
	SPHERE,
	PLANE,
	POINT,
	CUBE
};
struct Vertex {
	glm::vec3 Position;
	glm::vec3 Normal;
	glm::vec2 TexCoords;
	glm::vec3 Tangent;
	glm::vec3 Bitangent;

	int m_BoneIDs[MAX_BONE_INFLUENCE];
	float m_Weights[MAX_BONE_INFLUENCE];
};

typedef unsigned int GLuint;
class Mesh 
{
public:
	std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
	GLuint VAO, VBO, EBO;
	std::string name;

	BoundingSphere bs;

public:
	Mesh();
	Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
	static std::shared_ptr<Mesh> initSphere(int pointNum); 
	static std::shared_ptr<Mesh> initPlane();
	static std::shared_ptr<Mesh> initPoint(); 
	static std::shared_ptr<Mesh> initCube();
	void genVAO();
};

class MeshFilter :public Component {
public:
	MeshFilter();

	~MeshFilter();

	void addShape(SHAPE type);
	//void loadMesh(std::string path);
	void addMesh(std::shared_ptr<Mesh> mesh_);

	virtual void loadFromJson(json& data);

public:
	std::vector<std::shared_ptr<Mesh>> meshes;
private:
	void addShape(std::string type);
};

