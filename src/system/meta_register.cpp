#include"../renderer/Mesh_Filter.h"
#include"../renderer/Mesh_Renderer.h"
#include"../component/transform.h"
//#include"meta_register.h"
//#include<meta/meta.hpp>
//#include<meta/factory.hpp>
//
//MetaRegister::MetaRegister() {
//	std::hash<std::string_view> hash{};
//	
//	meta::reflect<Transform>(hash("Transform"))
//		.ctor()
//		.data<&Transform::position>(hash("Position"))
//		.data<&Transform::rotation>(hash("Rotation"))
//		.data<&Transform::scale>(hash("scale"));
//}
//
//MetaRegister::~MetaRegister() {
//
//}