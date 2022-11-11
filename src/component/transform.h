#pragma once
#include"Component.h"
#include<glm/glm.hpp>
#include <unordered_map>
#include"../../include/yaml-cpp/yaml.h"

const float sca_scaler = 0.012f;
const float pos_scaler = 8.0f;

class Transform : public Component {
public:
	Transform();
	~Transform();

	using Components = std::unordered_map<std::string, YAML::Node>;
	static std::shared_ptr<Transform> GetWorldTransform(Components& comps, std::string id);
protected:
	static std::shared_ptr<Transform> InitNode(YAML::Node trans_node);
	void SetParentTransform(std::shared_ptr<Transform> parent);

public:
	glm::vec3 position;
	glm::vec3 rotation;
	glm::vec3 scale; 
};
