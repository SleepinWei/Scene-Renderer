#pragma once
#include<memory>
#include"Component.h"

class Shader;
class SSBO;

class Grass :public Component {
public:
	Grass();
	~Grass();
	void init();

	void constructCall();
	void prepareData();

	void render(const std::shared_ptr<Shader>& shader);
public:
	std::shared_ptr<Shader> shader; //shader for deferred pipeline
	std::shared_ptr<Shader> shadowShader; // shader for shadows 
	std::shared_ptr<Shader> computeShader; // shader for computations
	std::shared_ptr<SSBO> outPoseBuffer;
	std::shared_ptr<SSBO> grassPatchesBuffer;
	std::shared_ptr<SSBO> indirectBuffer;
	// terrain data : get from terrain component
	bool initDone;

	unsigned int VAO, VBO;
};
