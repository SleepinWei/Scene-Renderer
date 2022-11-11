#include"transform.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

Transform::Transform() :position(0.f), rotation(0.f), scale(1.0f) {
	Component::name = "Transform";

}
Transform::~Transform() {

}

void Transform::loadFromJson(json& data) {
	glm::vec3 position_(
		data["position"][0].get<float>(),
		data["position"][1].get<float>(),
		data["position"][2].get<float>()
	);
	glm::vec3 rotation_(
		data["rotation"][0].get<float>(),
		data["rotation"][1].get<float>(),
		data["rotation"][2].get<float>()
	);
	glm::vec3 scale_(
		data["scale"][0].get<float>(),
		data["scale"][1].get<float>(),
		data["scale"][2].get<float>()
	);
	this->position = position_;
	this->rotation = rotation_;
	this->scale = scale_;
}
// 构造一个游戏对象的世界坐标Transform
std::shared_ptr<Transform> Transform::GetWorldTransform(Components& comps, std::string id)
{
	// 构造所有父物体的Transform对象
	auto node = comps[id]["Transform"];
	auto trans = Transform::InitNode(node);
	auto parents = std::vector<std::shared_ptr<Transform>>();
	auto temp = node["m_Father"]["fileID"];
	std::string father = node["m_Father"]["fileID"].as<std::string>();
	while (father != "0")
	{
		parents.push_back(Transform::InitNode(comps[father]["Transform"]));
		father = comps[father]["Transform"]["m_Father"]["fileID"].as<std::string>();
	}
	
	// 进行坐标系转换
	for (auto parent : parents)
		trans->SetParentTransform(parent);
	trans->scale *= sca_scaler;
	return trans;
}

// 从一个Transform节点构造Transform对象
std::shared_ptr<Transform> Transform::InitNode(YAML::Node trans_node)
{
	auto trans = std::make_shared<Transform>();
	YAML::Node pos = trans_node["m_LocalPosition"];
	YAML::Node rot = trans_node["m_LocalRotation"];
	YAML::Node scale = trans_node["m_LocalScale"];

	trans->position = glm::vec3(pos["x"].as<float>(), pos["y"].as<float>(), pos["z"].as<float>());
	trans->rotation = glm::eulerAngles(glm::quat(rot["w"].as<float>(), rot["x"].as<float>(), rot["y"].as<float>(), rot["z"].as<float>()));
	trans->rotation.x = glm::degrees(trans->rotation.x);
	trans->rotation.y = -glm::degrees(trans->rotation.y);
	trans->rotation.z = glm::degrees(trans->rotation.z);
	trans->scale = glm::vec3(scale["x"].as<float>(), scale["y"].as<float>(), scale["z"].as<float>());

	return trans;
}

// 将一个Transform的数值与一个父对象进行运算 得到父对象坐标系下的结果
void Transform::SetParentTransform(std::shared_ptr<Transform> parent)
{
	glm::vec3 p_pos = parent->position;
	glm::vec3 p_rot = parent->rotation;
	glm::vec4 pos = glm::scale(parent->scale) * 
		glm::eulerAngleYXZ(glm::radians(p_rot.y), glm::radians(p_rot.x), glm::radians(p_rot.z)) * 
		glm::vec4(position.x,position.y, position.z, 1.0f);
	position = glm::vec3(pos.x + p_pos.x, pos.y + p_pos.y, pos.z + p_pos.z);

	rotation = glm::degrees(glm::eulerAngles(glm::quat(glm::radians(p_rot)) * glm::quat(glm::radians(rotation))));
	scale *= parent->scale;
}