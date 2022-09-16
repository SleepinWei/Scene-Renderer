#include<glad/glad.h>
#include"Terrain.h"
#include<stb/stb_image.h>
#include<glm/gtc/matrix_transform.hpp>
#include"src/Utils.h"

void Terrain::initGeometry( ){
	const std::string& path = "./asset/heightmap/iceland/";
	int width, height, nrChannels; 
	yScale = 1.0f;
	yShift = 1.0f; 
	//xzScale = 10.0f;
	model = glm::mat4(1);
	// set the texture wrapping parameters
	unsigned char* data = stbi_load((path+"heightmap.png").c_str(), &width, &height, &nrChannels, 0);
	stbi_image_free(data);

	// indices
	float xzScale = 20.0f/std::max(width,height);
	//float xzScale = 1.0f;
	model[0][0] = xzScale;//x scale
	model[2][2] = xzScale; //z scale

	rez = 40;
	vertices = std::vector<float>(20 * rez * rez, 0);
	for (unsigned i = 0; i <= rez - 1; i++)
	{
		for (unsigned j = 0; j <= rez - 1; j++)
		{
			int index = 20 * (i * rez + j);
			vertices[index] = - width / 2.0f + width * i / (float)rez; // v.x
			vertices[index+1] = 0.0f; // v.y
			vertices[index + 2] = (-height / 2.0f + height * j / (float)rez); // v.z
			vertices[index + 3] = (i / (float)rez); // u
			vertices[index + 4] =  (j / (float)rez); // v

			vertices[index + 5] = (-width / 2.0f + width * (i + 1) / (float)rez); // v.x
			vertices[index + 6] = (0.0f); // v.y
			vertices[index + 7] = (-height / 2.0f + height * j / (float)rez); // v.z
			vertices[index+8] = ((i + 1) / (float)rez); // u
			vertices[index+9] = (j / (float)rez); // v

			vertices[index+10] = (-width / 2.0f + width * i / (float)rez); // v.x
			vertices[index+11] = (0.0f); // v.y
			vertices[index+12] = (-height / 2.0f + height * (j + 1) / (float)rez); // v.z
			vertices[index+13] = (i / (float)rez); // u
			vertices[index+14] = ((j + 1) / (float)rez); // v

			vertices[index+15] = (-width / 2.0f + width * (i + 1) / (float)rez); // v.x
			vertices[index+16] = (0.0f); // v.y
			vertices[index+17] = (-height / 2.0f + height * (j + 1) / (float)rez); // v.z
			vertices[index+18] = ((i + 1) / (float)rez); // u
			vertices[index+19] = ((j + 1) / (float)rez); // v
		}
	}
	//std::cerr << "Vertices Done" << '\n';
}

void Terrain::initVertexObject() {
	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

	//position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	//std::cerr << "Done1" << '\n';
	glPatchParameteri(GL_PATCH_VERTICES, NUM_PATCH_PTS);
}

Terrain::~Terrain() {
	glDeleteBuffers(1,&VBO);
	glDeleteVertexArrays(1,&VAO);
}
void Terrain::registerShader(ShaderType st){
	shader = renderManager.registerShader(st);
	for (auto material:materials){
		material->registerShader(st);
	}
}

void Terrain::render() {
	shader->setFloat("yShift", yShift);
	shader->setFloat("yScale", yScale);
	int beginIndex = 0;
	for (auto material : materials) {
		material->setBeginIndex(beginIndex);
		material->render();
		beginIndex += material->getMaterialCount();
	}

	shader->setMat4("model", model);

	glBindVertexArray(VAO);
	glDisable(GL_CULL_FACE);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glDrawArrays(GL_PATCHES, 0, NUM_PATCH_PTS * rez * rez);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_CULL_FACE);
}

Terrain::Terrain(TEX_TYPE tex1,TEX_TYPE tex2, ShaderType st):NUM_PATCH_PTS(4) {
	initGeometry();
	initVertexObject();
	materials.push_back(resourceManager.registerResource(tex1));
	materials.push_back(resourceManager.registerResource(tex2));
	registerShader(st);
}
