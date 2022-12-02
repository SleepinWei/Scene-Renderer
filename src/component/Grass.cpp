#include<glad/glad.h>
#include"Grass.h"
#include"../utils/Shader.h"
#include"../component/GameObject.h"
#include"../component/TerrainComponent.h"
#include"../buffer/SSBO.h"
#include"../buffer/ImageTexture.h"

Grass::Grass() {
	Component::name = "Grass";
	initDone = false;
	shader = nullptr;
	outPoseBuffer = nullptr;
	indirectBuffer = nullptr;
	VAO = VBO = 0;
}

Grass::~Grass() {

}

void Grass::init() {
	// init textures
	shader = std::make_shared<Shader>("./src/shader/grass/grass.vs","./src/shader/grass/grass.fs");
	computeShader = std::make_shared<Shader>("./src/shader/grass/grass.comp");

	outPoseBuffer = std::make_shared<SSBO>(1024 * 1024 * 1);
	grassPatchesBuffer = std::make_shared<SSBO>(1024 * 1024 * 32); 
	indirectBuffer = std::make_shared<SSBO>(32);
}

void Grass::prepareData() {
	// indirect buff
	if (!initDone) {
		initDone = true;
		init();
	}
	indirectBuffer->bindBuffer();
	if (indirectBuffer->dirty) {
		unsigned int three= 3;
		unsigned int zero = 0;
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,4, &three);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8, 4, &zero);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 12, 4, &zero);
		indirectBuffer->dirty = false;
	}

	// update every frame
	int zero = 0;
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 4, 4, &zero);

	// grass Patches
	grassPatchesBuffer->bindBuffer();
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 4, &zero); // grassCnt = 0;
	if (grassPatchesBuffer->dirty) {
		unsigned int one = 1;
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 4, 4, &one);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 8, 4, &one);
		grassPatchesBuffer->dirty = false;
	}
}

void Grass::constructCall() {
	this->grassPatchesBuffer->setBinding(0);
	this->outPoseBuffer->setBinding(1);
	this->indirectBuffer->setBinding(2);

	//debug
	//grassPatchesBuffer->bindBuffer();
	//void* data = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
	//unsigned int* u_data = (unsigned int*)data;
	//FILE* fp = fopen("./output.txt", "w");
	//fprintf(fp, "%d %d %d %d \n", u_data[0], u_data[1], u_data[2], u_data[3]);
	//auto f_data = (float*)data;
	//int index = 4;
	//for (int i = 0; i < 100; i++) {
	//	fprintf(fp, "Position:\n");
	//	for (int j = 0; j < 4; j++) {
	//		//position
	//		for (int k = 0; k < 4; k++) {
	//			//float 
	//			fprintf(fp, "%f ", f_data[index++]);
	//		}
	//		fprintf(fp,"\n");
	//	}
	//	fprintf(fp, "Tex: \n");
	//	for (int j = 0; j < 4; j++) {
	//		//texcoords;
	//		for (int k = 0; k < 2; k++) {
	//			fprintf(fp, "%f ", f_data[index++]);
	//		}
	//		fprintf(fp,"\n");
	//	}
	//}
	//fclose(fp);
	//glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
	computeShader->use();

	glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, this->grassPatchesBuffer->ssbo);
	glDispatchComputeIndirect(0);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void Grass::render(const std::shared_ptr<Shader>& outShader) {
	auto actualShader = (outShader == nullptr) ? this->shader : outShader;
	actualShader->use();

	if (VAO == 0) {
		float data[] = {
		   -0.05f,0.0f,0.0f,   0.0f,0.0f,-1.0f,0.0f,0.0f,// x
			0.0f,0.25f,0.0f,   0.0f,0.0f,-1.0f,0.5f,1.0f,// y
			0.0f,0.0f,0.05f,   0.0f,0.0f,-1.0f,1.0f,0.0f // z
		};
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO);

		glBindVertexArray(VAO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER,sizeof(data),data,GL_STATIC_DRAW);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
	}

	glBindVertexArray(VAO);
	//debug
	//indirectBuffer->bindBuffer();
	//auto data = (unsigned int*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
	//auto fp = fopen("./output.txt", "w");
	//fprintf(fp, "%d,%d,%d,%d\n", data[0], data[1], data[2], data[3]);
	//glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
	//fclose(fp);

	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer->ssbo); // indirect draw

	glDisable(GL_CULL_FACE);
	glDrawArraysIndirect(GL_TRIANGLES, (void*)0);
	glEnable(GL_CULL_FACE);
}
