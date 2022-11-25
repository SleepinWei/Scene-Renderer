#include<glad/glad.h>
#include"GameObject.h"
#include"../component/Component.h"
const float PI = 3.1415926f;
#include<glm/gtc/matrix_transform.hpp>
#include<iostream>
#include"../renderer/Mesh_Filter.h"
#include"../renderer/Mesh_Renderer.h"
#include"../component/transform.h"
#include"../system/meta_register.h"
//#include<rttr/registration.h>

GameObject::GameObject() {
	this->m_isDeferred = true; // true by default
}

GameObject::GameObject(std::string name) {
	name = name;
	this->m_isDeferred = true;
}

GameObject::~GameObject() {

}

//std::shared_ptr<Component> GameObject::addComponent(std::string component_type_name) {
//	//rttr::type t = rttr::type::get_by_name(component_type_name);
//	//rttr::variant var = t.create();    // ´´½¨ÊµÀý
//
//	//std::shared_ptr<Component> component = var.get_value<std::shared_ptr<Component>>();
//	// TODO: improve  this part with reflection system
//	std::shared_ptr<Component> component;
//	if (component_type_name == "Transform") {
//		component = std::make_shared<Transform>();
//	}
//	else if (component_type_name == "MeshFilter") {
//		component = std::make_shared<MeshFilter>();
//	}
//	else if (component_type_name == "MeshRenderer") {
//		component = std::make_shared<MeshRenderer>(); 
//	}
//	component->setGameObject(shared_from_this());
//
//	if (component_type_instance_map.find(component_type_name) == component_type_instance_map.end()) {
//		std::vector<std::shared_ptr<Component>> component_vec;
//		component_vec.push_back(component);
//		component_type_instance_map[component_type_name] = component_vec;
//	}
//	else {
//		component_type_instance_map[component_type_name].push_back(component);
//	}
//	return component;
//}

//std::shared_ptr<Component>& GameObject::GetComponents(std::string component_type_name) {
	//return component_type_instance_map[component_type_name];
//}

std::shared_ptr<Component> GameObject::GetComponent(std::string component_type_name) {
	if (component_type_instance_map.find(component_type_name) == component_type_instance_map.end()) {
		return nullptr;
	}
	return component_type_instance_map[component_type_name];
}

void GameObject::loadFromJson(json& data) {
	std::string name_ = data["name"].get<std::string>();
	this->name = name_;
	if (data.find("components") != data.end()) {
		json components = data["components"];
		for (auto iter = components.begin(); iter != components.end(); ++iter) {
			auto comp = Meta::generateComponent(iter.key());
			comp->loadFromJson(iter.value());

			this->addComponent(comp);
		}
	}
	else {
		//error
	}
	if (data.find("isDeferred") != data.end()) {
		auto _isDeferred = data["isDeferred"].get<bool>();
		this->setDeferred(_isDeferred);
	}
	else {
		// no error
	}
}

std::shared_ptr<GameObject> GameObject::addComponent(const std::shared_ptr<Component>& component)
{
	component->setGameObject(shared_from_this());
	std::string component_type_name = component->name;
	if (component_type_instance_map.find(component_type_name) == component_type_instance_map.end()) {
		//std::vector<std::shared_ptr<Component>> component_vec;
		//component_vec.push_back(component);
		component_type_instance_map[component_type_name] = component;
	}
	else {
		//component_type_instance_map[component_type_name] = component;
		std::cout << "Component: " << component_type_name << " has already existed\n";
	}
	return shared_from_this();
}

bool GameObject::isDeferred()const {
	return this->m_isDeferred;
}

bool GameObject::setDeferred(bool _isDeferred) {
	this->m_isDeferred = _isDeferred;
}
