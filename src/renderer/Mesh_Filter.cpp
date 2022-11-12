#include<glad/glad.h>
#include<glfw/glfw3.h>
#include"Mesh_Filter.h"
#include"Material.h"
#include"Texture.h"
#include"../component/Model.h"
#include<glfw/glfw3.h>

#define PI 3.1415926

Mesh::Mesh() {
	VAO = 0, VBO = 0, EBO = 0;
}

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices){
	this->vertices = vertices;
	this->indices = indices;
	VAO = 0, VBO = 0, EBO = 0;
}

//void MeshFilter::loadMesh(std::string path) {
	// TODO: 
//}

std::shared_ptr<Mesh> Mesh::initSphere(int pointNum) {
	std::shared_ptr<Mesh> result = std::make_shared<Mesh>();
	float delta = 2 * PI * 1.0 / pointNum; // delta of angle
	float R = 1;
	int overallPointNum = (pointNum/2 + 1) * (pointNum + 1);
	std::vector<Vertex>(overallPointNum * 3).swap(result->vertices);

	int overallTriangles = pointNum * pointNum;
	std::vector<GLuint>(overallTriangles * 3,0).swap(result->indices);
	auto& vertices = result->vertices;
	auto& indices = result->indices;

	//vertices[0].Position = { 0,R,0 };
	//vertices[0].TexCoords = { 0.5,0 };
	//vertices[0].Normal = vertices[0].Position;
	int index = 0;
	for (int i = 0; i < pointNum / 2 + 1; i++) {
		for (int j = 0; j <= pointNum; j++) {
			float theta = i * delta;
			float phi = j * delta;
			vertices[index].Position = { 
				R * sin(theta) * sin(phi),
				R * cos(theta) ,
				R * sin(theta) * cos(phi) 
			};
			vertices[index].TexCoords = {
				j * 1.0 / (pointNum),
				i * 1.0 / (pointNum / 2)
			};
			vertices[index].Normal = vertices[index].Position;
			++index;
		}
	}
	//vertices[index].Position = { 0,-R,0 };
	//vertices[index].TexCoords = { 0.5,1 };
	//vertices[index].Normal = vertices[index].Position;
	// set indices; 
	index = 0; 
	/*for (int i = 0; i < pointNum; i++) {
		indices[index++] = 0; 
		indices[index++] = i + 1;
		indices[index++] = i + 2; 
	}*/
	for (int i = 0; i < pointNum/2; i++) {
		int basicPointIndex = i * (pointNum+1);
		for (int j = 0; j < pointNum; j++) {
			int curPoint = basicPointIndex + j; 
			//int nextPoint =(j != pointNum -1 ? curPoint + 1 : basicPointIndex); 
			int nextPoint = curPoint + 1;
			indices[index++] = curPoint;
			indices[index++] = curPoint + pointNum + 1; 
			indices[index++] = nextPoint; 
			indices[index++] = curPoint + pointNum + 1; 
			indices[index++] = nextPoint + pointNum + 1;
			indices[index++] = nextPoint;
		}
	}
	//int basicPointIndex = (pointNum / 2 - 2) * (pointNum+1) + 1;
	//for (int i = 0; i < pointNum; i++) {
		//int curPoint = basicPointIndex + i;
		//int nextPoint = (i != pointNum - 1 ? curPoint + 1 : basicPointIndex);
		//int nextPoint = curPoint + 1;
		//indices[index++] = curPoint;
		//indices[index++] = overallPointNum - 1;
		//indices[index++] = nextPoint;
	//}
	return result;
}

std::shared_ptr<Mesh> Mesh::initPlane() {
	std::shared_ptr<Mesh> result = std::make_shared<Mesh>();
	std::vector<Vertex>(4).swap(result->vertices);

	auto& vertices = result->vertices;
	std::vector<unsigned int>{
		0, 1, 2,
		0, 2, 3
	}.swap(result->indices);

	vertices[0].Position = { 1.0f, 0.0f, 1.0f };
	vertices[1].Position = { 1.0f, 0.0f, -1.0f };
	vertices[2].Position = { -1.0f, 0.0f, -1.0f };
	vertices[3].Position = { -1.0f, 0.0f, 1.0f };
	vertices[0].Normal = { 0.0,1.0,0.0 };
	vertices[1].Normal = { 0.0,1.0,0.0 };
	vertices[2].Normal = { 0.0,1.0,0.0 };
	vertices[3].Normal = { 0.0,1.0,0.0 };
	vertices[0].TexCoords = {1.0f,1.0f };
	vertices[1].TexCoords = { 1.0f,0.0f };
	vertices[2].TexCoords = { 0.0f,0.0f };
	vertices[3].TexCoords = { 0.0f,1.0f };
	return result;
}

std::shared_ptr<Mesh> Mesh::initPoint() {
	std::shared_ptr<Mesh> result = std::make_shared<Mesh>();
	std::vector<Vertex>(1).swap(result->vertices);
	std::vector<unsigned int>{ 0 }.swap(result->indices);

	auto& vertices = result->vertices;
	//auto& indices = result->indices;

	vertices[0].Position = { 0,0,0 };
	return result;
}

std::shared_ptr<Mesh> Mesh::initCube() {
	std::shared_ptr<Mesh> result = std::make_shared<Mesh>();
	std::vector<Vertex>(8).swap(result->vertices);

	auto& vertices = result->vertices;
	vertices[0].Position = { 1.0f,1.0f,1.0f };
	vertices[1].Position = { 1.0f,1.0f,-1.0f };
	vertices[2].Position = { -1.0f,1.0f,-1.0f };
	vertices[3].Position = { -1.0f,1.0f,1.0f };
	vertices[4].Position = { 1.0f,-1.0f,1.0f };
	vertices[5].Position = { 1.0f,-1.0f,-1.0f };
	vertices[6].Position = { -1.0f,-1.0f,-1.0f };
	vertices[7].Position = { -1.0f,-1.0f,1.0f };

	vertices[0].Normal= { 1.0f,1.0f,1.0f };
	vertices[1].Normal= { 1.0f,1.0f,-1.0f };
	vertices[2].Normal= { -1.0f,1.0f,-1.0f };
	vertices[3].Normal= { -1.0f,1.0f,1.0f };
	vertices[4].Normal= { 1.0f,-1.0f,1.0f };
	vertices[5].Normal= { 1.0f,-1.0f,-1.0f };
	vertices[6].Normal= { -1.0f,-1.0f,-1.0f };
	vertices[7].Normal= { -1.0f,-1.0f,1.0f };

	std::vector<unsigned int>{
		0,4,1,
		1,4,5,
		0,3,4,
		3,7,4,
		3,2,7,
		2,6,7,
		0,2,1,
		5,6,1,
		0,2,3,
		1,2,0,
		4,6,5,
		4,7,6
	}.swap(result->indices);

	return result;
}

MeshFilter::MeshFilter() {
	Component::name = "MeshFilter";
}

MeshFilter::~MeshFilter() {

}

void MeshFilter::addShape(SHAPE shape) {
	std::shared_ptr<Mesh> mesh;
	switch (shape)
	{
	case SHAPE::SPHERE:
		mesh = Mesh::initSphere(64);
		break;
	case SHAPE::PLANE:
		mesh = Mesh::initPlane();
		break;
	case SHAPE::CUBE:
		mesh = Mesh::initCube();
		break;
	case SHAPE::POINT:
		mesh = Mesh::initPoint();
		break;
	default:
		break;
	}
	meshes.push_back(mesh);
}

void MeshFilter::addShape(std::string type) {
	std::shared_ptr<Mesh> mesh;
	if (type == "sphere") {
		mesh = Mesh::initSphere(64);
	}
	else if (type == "plane") {
		mesh = Mesh::initPlane();
	}
	else if (type == "cube") {
		mesh = Mesh::initCube();
	}
	else if (type == "point") {
		mesh = Mesh::initPoint();
	}
	meshes.push_back(mesh);
}

void MeshFilter::addMesh(std::shared_ptr<Mesh> mesh_) {
	meshes.push_back(mesh_);
}

void MeshFilter::loadFromJson(json& data) {
	for (auto iter = data.begin(); iter != data.end(); ++iter) {
		// mesh name : mesh path
		if (iter.key() == "shape") {
			std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();
			this->addShape(iter.value().get<std::string>());
		}
		else {
			std::shared_ptr<Mesh> mesh = Model::loadModel(iter.value().get<std::string>());
			mesh->name = iter.key();
			this->addMesh(mesh);
		}
	}
}

void Mesh::genVAO() {
	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
		&(vertices[0]), GL_STATIC_DRAW);
	glGenBuffers(1, &EBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
		&(indices[0]), GL_STATIC_DRAW);

	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);
	{
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(Vertex), (void*)offsetof(Vertex, Position));
		glVertexAttribPointer(1, 3, GL_FLOAT, false, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
		glVertexAttribPointer(2, 2, GL_FLOAT, false, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
		glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
		glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glEnableVertexAttribArray(3);
		glEnableVertexAttribArray(4);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Material

}

