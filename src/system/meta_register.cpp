#include"component/Mesh_Filter.h"
#include"component/Mesh_Renderer.h"
#include"component/transform.h"
#include"component/Component.h"
#include"component/Lights.h"
#include"system/meta_register.h"
#include<glm/glm.hpp>

Meta::Meta() {
	// Transform 
}

Meta::~Meta() {

}

std::shared_ptr<Component> Meta::generateComponent(const std::string& name) {
	if (name == "Transform") {
		std::shared_ptr<Transform> transform = std::make_shared<Transform>();
		return std::static_pointer_cast<Component>(transform);
	}
	else if (name == "MeshFilter") {
		std::shared_ptr<MeshFilter> meshfilter = std::make_shared<MeshFilter>();
		return std::static_pointer_cast<Component>(meshfilter);
	}
	else if (name == "MeshRenderer") {
		std::shared_ptr<MeshRenderer> meshrenderer = std::make_shared<MeshRenderer>();
		return std::static_pointer_cast<Component>(meshrenderer);
	}
	else if (name == "PointLight") {
		std::shared_ptr<PointLight> pointLight = std::make_shared<PointLight>();
		return std::static_pointer_cast<Component>(pointLight);
	}
	else if (name == "DirectionLight") {
		std::shared_ptr<DirectionLight> directionLight = std::make_shared<DirectionLight>();
		return std::static_pointer_cast<Component>(directionLight);
	}
	else if (name == "SpotLight") {
		std::shared_ptr<SpotLight> spotLight = std::make_shared<SpotLight>();
		return std::static_pointer_cast<Component>(spotLight);
	}
	return nullptr;
	//if(name == "")
}