#include"Mesh_Filter.h"

#define PI 3.1415926
//using namespace rttr;
//RTTR_REGISTRATION{
//	registration::class_<MeshFilter>("MeshFilter")
//	.constructor<>()(rttr::policy::ctor::as_std_shared_ptr);
//	//.constructor<>()(rttr::policy::ctor::as_raw_ptr);
//}

MeshFilter::MeshFilter(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::shared_ptr<Material> material){
	this->vertices = vertices;
	this->indices = indices;
	this->material = material;
}

void MeshFilter::loadMesh(std::string path) {
	// TODO: 
}

MeshFilter::~MeshFilter() {

}
void MeshFilter::addMaterial(std::shared_ptr<Material> material) {
	material = material;
}

void MeshFilter::initSphere(int pointNum) {
	float delta = 2 * PI * 1.0 / pointNum; // delta of angle
	float R = 1;
	int overallPointNum = (pointNum/2-1) * (pointNum + 1) + 2;
	vertices = std::vector<Vertex>(overallPointNum * 3);

	int overallTriangles = pointNum * 2 * (pointNum / 2 - 1);
	indices = std::vector<GLuint>(overallTriangles * 3,0);

	vertices[0].Position = { 0,R,0 };
	vertices[0].TexCoords = { 0.5,0 };
	vertices[0].Normal = vertices[0].Position;
	int index = 1;
	for (int i = 0; i < pointNum / 2 - 1; i++) {
		for (int j = 0; j <= pointNum; j++) {
			float theta = (i + 1) * delta;
			float phi = j * delta;
			vertices[index].Position = { 
				R * sin(theta) * sin(phi),
				R * cos(theta) ,
				R * sin(theta) * cos(phi) 
			};
			vertices[index].TexCoords = {
				j * 1.0 / (pointNum),
				(i + 1) * 1.0 / (pointNum / 2)
			};
			vertices[index].Normal = vertices[index].Position;
			++index;
		}
	}
	vertices[index].Position = { 0,-R,0 };
	vertices[index].TexCoords = { 0.5,1 };
	vertices[index].Normal = vertices[index].Position;
	// set indices; 
	index = 0; 
	for (int i = 0; i < pointNum; i++) {
		indices[index++] = 0; 
		indices[index++] = i + 1;
		indices[index++] = i + 2; 
	}
	for (int i = 0; i < pointNum/2-2; i++) {
		int basicPointIndex = i * (pointNum+1) + 1;
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
	int basicPointIndex = (pointNum / 2 - 2) * (pointNum+1) + 1;
	for (int i = 0; i < pointNum; i++) {
		int curPoint = basicPointIndex + i;
		//int nextPoint = (i != pointNum - 1 ? curPoint + 1 : basicPointIndex);
		int nextPoint = curPoint + 1;
		indices[index++] = curPoint;
		indices[index++] = overallPointNum - 1;
		indices[index++] = nextPoint;
	}
}

void MeshFilter::initPlane() {

	vertices = std::vector<Vertex>(4);
	std::vector<unsigned int> indices = {
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
}

MeshFilter::MeshFilter(SHAPE shape) {
	switch (shape)
	{
	case SHAPE::SPHERE:
		initSphere(64);
		break;
	case SHAPE::PLANE:
		initPlane();
		break;
	default:
		break;
	}
}
