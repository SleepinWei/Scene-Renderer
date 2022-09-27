#pragma once
#include<vector>
#include<glm/glm.hpp>
#include<memory>
#include"../utils/Shader.h"
// #include<meta/meta.hpp>
#include<unordered_map>
#include<string>
class Component; 

class GameObject:public std::enable_shared_from_this<GameObject> {
public:
	GameObject() = default;
	GameObject(std::string name);
	virtual ~GameObject();

	std::shared_ptr<Component> addComponent(std::string component_type_name);

	template<class T>
	std::shared_ptr<T> addComponent() {
		std::shared_ptr<T> component = std::make_shared<T>();
		//rttr::type t = rttr::type::get(*component);
		std::string component_type_name = component->name; 
		component->set_game_object(shared_from_this());
		if (component_type_instance_map.find(component_type_name) == component_type_instance_map.end()) {
			std::vector<std::shared_ptr<Component>> component_vec;
			component_vec.push_back(component);
			component_type_instance_map[component_type_name] = component_vec;
		}
		else {
			component_type_instance_map[component_type_name].push_back(component);
		}
		return component;
	}

	std::shared_ptr<Component> GetComponent(std::string component_type_name);

	// get all components with the same name 
	std::vector<std::shared_ptr<Component>>& GetComponents(std::string component_type_name);

public:
	std::string name; 
	std::unordered_map<std::string, std::vector<std::shared_ptr<Component>>> component_type_instance_map;
};

