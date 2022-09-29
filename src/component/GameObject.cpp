#include<glad/glad.h>
#include"GameObject.h"
#include"../component/Component.h"
const float PI = 3.1415926f;
#include<glm/gtc/matrix_transform.hpp>
#include<iostream>
#include"../renderer/Mesh_Filter.h"
#include"../renderer/Mesh_Renderer.h"
#include"../component/transform.h"
//#include<rttr/registration.h>

GameObject::GameObject(std::string name) {
	name = name;
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

std::vector<std::shared_ptr<Component>>& GameObject::GetComponents(std::string component_type_name) {
	return component_type_instance_map[component_type_name];
}

std::shared_ptr<Component> GameObject::GetComponent(std::string component_type_name) {
	if (component_type_instance_map.find(component_type_name) == component_type_instance_map.end()) {
		return nullptr;
	}
	if (component_type_instance_map[component_type_name].size() == 0) {
		return nullptr;
	}
	return component_type_instance_map[component_type_name][0];
}



