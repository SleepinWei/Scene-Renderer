#pragma once
#include"../component/Component.h"

class Shader;
class Cloud :public Component {
public:
	Cloud() {
		Component::name = "Cloud";
	}
	~Cloud() {}

	void render(const std::shared_ptr<Shader>& outShader);
	void constructCall();

public:
	// objects
	std::shared_ptr<Shader> shader;
};
