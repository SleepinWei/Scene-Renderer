#include"../renderer/Mesh_Filter.h"
#include"../renderer/Mesh_Renderer.h"
#include"../component/transform.h"
#include"../component/Component.h"
#include"meta_register.h"
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
	//if(name == "")
}