#pragma once

#include"../component/Component.h"
#include<string>
#include<memory>
#include<glm/glm.hpp>
#include<vector>

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

class MeshFilter :public Component {
public:

	struct Vertex {
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoords;
		glm::vec3 Tangent;
		glm::vec3 Bitangent;

		int m_BoneIDs[MAX_BONE_INFLUENCE];
		float m_Weights[MAX_BONE_INFLUENCE];
	};

	MeshFilter();
	MeshFilter(std::vector<Vertex> vertices, std::vector<unsigned int> indices);

	~MeshFilter();

	void loadShape(SHAPE type);
	void loadMesh(std::string path);

private:
	void initSphere(int pointNum); 
	void initPlane();
	void initPoint(); 
	void initCube();

public:
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

