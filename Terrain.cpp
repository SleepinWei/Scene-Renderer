#include<glad/glad.h>
#include"Terrain.h"
#include<stb/stb_image.h>

void Terrain::initGeometry(const std::string& path) {
	int width, height, nChannel; 
	unsigned char* data = stbi_load(path.c_str(), &width, &height, &nChannel, 0);
	vertices = std::vector<float>(3 * height * width, 0);
	yScale = 0.5f / 256.0f;
	yShift = -1.0f; 
	xzScale = 10.0f;
	int index = 0; 
	// vertices
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			unsigned char* texel = data + (j + width * i) * nChannel;
			unsigned char y = texel[0]; 
			vertices[3 * index] = (- height / 2.0f + i) *1.0/ height *xzScale;
			vertices[3 * index + 1] = (int)y * yScale - yShift;
			vertices[3 * index + 2] = (-width / 2.0f + j) *1.0 / width *xzScale;
			index++; 
		}
	}
	stbi_image_free(data);
	// indices
	indices = std::vector<unsigned int>((height - 1) * width * 2);
	index = 0;
	for (unsigned int i = 0; i < height - 1; i++)       // for each row a.k.a. each strip
	{
		for (unsigned int j = 0; j < width; j++)      // for each column
		{
			for (unsigned int k = 0; k < 2; k++)      // for each side of the strip
			{
				indices[index] = j + width * (i + k);
				index++;
			}
		}
	}
	NUM_STRIPS = height - 1;
	NUM_VERTS_PER_STRIP = width * 2;
}

void Terrain::initVertexObject() {
	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

	//position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
	glEnableVertexAttribArray(0);

	glGenBuffers(1, &EBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		indices.size() * sizeof(unsigned int),
		&indices[0], GL_STATIC_DRAW);
}

Terrain::~Terrain() {

}

void Terrain::render(Shader& shader) {
	shader.use();
	shader.setFloat("shift", yShift);
	shader.setFloat("scale", yScale);

	glBindVertexArray(VAO);
	//glDisable(GL_CULL_FACE);
	glFrontFace(GL_CW);
	for (unsigned int strip = 0; strip < NUM_STRIPS; ++strip)
	{
		glDrawElements(GL_TRIANGLE_STRIP,   // primitive type
			NUM_VERTS_PER_STRIP, // number of indices to render
			GL_UNSIGNED_INT,     // index data type
			(void*)(sizeof(unsigned int)
				* NUM_VERTS_PER_STRIP
				* strip)); // offset to starting index
	}
	glFrontFace(GL_CCW);
	//glEnable(GL_CULL_FACE);
}

Terrain::Terrain(const std::string& path) {
	initGeometry(path);
	initVertexObject();
}