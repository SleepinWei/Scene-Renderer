#pragma once

#include"../component/Component.h"
#include<string>
#include<memory>
#include<glm/glm.hpp>
#include<vector>
#include"Texture.h"
#include"Material.h"
#include<rttr/registration.h>

const int MAX_BONE_INFLUENCE = 4;
//const double PI = 3.1415926; 

enum class SHAPE {
	SPHERE,
	PLANE
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

	MeshFilter()=default;
	MeshFilter(SHAPE type);
	MeshFilter(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::shared_ptr<Material>);
	void addMaterial(std::shared_ptr<Material> material);

	~MeshFilter();

	void loadMesh(std::string path);

private:
	void initSphere(int pointNum); 
	void initPlane();

public:
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
	std::shared_ptr<Material> material;
};

