#pragma once
#include<memory>
#include"Component.h"
#include "component/VegetationSettings.h"

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
	VegetationSettings settings() const {checkLogicThread();return settings_;}
	void setSettings(VegetationSettings value) {checkLogicThread();value.validate();settings_=std::move(value);invalidate();}
	template<class F> void updateSettings(F&& edit) {auto value=settings();edit(value);setSettings(std::move(value));}
private:
	VegetationSettings settings_;
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
