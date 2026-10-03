#pragma once
#include<vector>
#include<stdexcept>
#include<glm/glm.hpp>
#include<memory>
#include"../utils/Shader.h"
// #include<meta/meta.hpp>
#include<unordered_map>
#include<string>
#include<json/json.hpp>

class Component; 
using json = nlohmann::json;

class GameObject:public std::enable_shared_from_this<GameObject> {
public:
	GameObject();
	GameObject(std::string name);
	virtual ~GameObject();

	//std::shared_ptr<Component> addComponent(std::string component_type_name);

	template<class T>
	std::shared_ptr<T> addComponent() {
		std::shared_ptr<T> component = std::make_shared<T>();
        const auto found=component_type_instance_map.find(component->name);
        if(found!=component_type_instance_map.end()){
            auto existing=std::dynamic_pointer_cast<T>(found->second);
            if(!existing)throw std::logic_error("Component name registered to another type");
            return existing;
        }
        addComponent(component);
		return component;
	}

	std::shared_ptr<GameObject> addComponent(const std::shared_ptr<Component>& component);
	std::shared_ptr<Component> GetComponent(std::string component_type_name) ;

	virtual void loadFromJson(json& data);
	bool isDeferred()const;
	void setDeferred(bool _isDeferred);

public:
	std::string name; 
	std::unordered_map<std::string, std::shared_ptr<Component>> component_type_instance_map;
	bool m_isDeferred; // render with forward or deferred
};

