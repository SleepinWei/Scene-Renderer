#include"Mesh_Filter.h"
#include"Material.h"
#include"Texture.h"
#include<glfw/glfw3.h>

#define PI 3.1415926
//using namespace rttr;
//RTTR_REGISTRATION{
//	registration::class_<MeshFilter>("MeshFilter")
//	.constructor<>()(rttr::policy::ctor::as_std_shared_ptr);
//	//.constructor<>()(rttr::policy::ctor::as_raw_ptr);
//}
MeshFilter::MeshFilter() {
	Component::name = "MeshFilter";
}

MeshFilter::MeshFilter(std::vector<Vertex> vertices, std::vector<unsigned int> indices){
	Component::name = "MeshFilter";
	this->vertices = vertices;
	this->indices = indices;
}

void MeshFilter::loadMesh(std::string path) {
	// TODO: 
}

MeshFilter::~MeshFilter() {

}

void MeshFilter::initSphere(int pointNum) {
	float delta = 2 * PI * 1.0 / pointNum; // delta of angle
	float R = 1;
	int overallPointNum = (pointNum/2 + 1) * (pointNum + 1);
	vertices = std::vector<Vertex>(overallPointNum * 3);

	int overallTriangles = pointNum * pointNum;
	indices = std::vector<GLuint>(overallTriangles * 3,0);

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
}

void MeshFilter::initPlane() {

	vertices = std::vector<Vertex>(4);
	indices = std::vector<unsigned int>{
		0, 1, 2,
		0, 2, 3
	};

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
}

void MeshFilter::initPoint() {
	vertices = std::vector<Vertex>(1);
	vertices[0].Position = { 0,0,0 };
	indices = std::vector<unsigned int>{ 0 };
}

void MeshFilter::initCube() {
	vertices = std::vector<Vertex>(8);
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

	indices = std::vector<unsigned int>{
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
	};

}

void MeshFilter::loadShape(SHAPE shape) {
	switch (shape)
	{
	case SHAPE::SPHERE:
		initSphere(64);
		break;
	case SHAPE::PLANE:
		initPlane();
		break;
	case SHAPE::CUBE:
		initCube();
		break;
	case SHAPE::POINT:
		initPoint();
		break;
	default:
		break;
	}
}
