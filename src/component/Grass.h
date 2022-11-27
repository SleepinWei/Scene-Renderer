#pragma once
#include<memory>
#include"Component.h"

class Shader;
class SSBO;

class Grass :public Component {
public:
	Grass();
	~Grass();
	void render(const std::shared_ptr<Shader>& shader);
public:
	std::shared_ptr<Shader> shader;
	std::shared_ptr<SSBO> outPoseBuffer;
	std::shared_ptr<SSBO> indirectBuffer;
};
