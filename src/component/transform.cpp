#include"transform.h"

//using namespace rttr;
//RTTR_REGISTRATION{
//	registration::class_<Transform>("Transform")
//		.constructor<>()(rttr::policy::ctor::as_std_shared_ptr)
//		.property("position",&Transform::position)
//		.property("rotation",&Transform::rotation)
//		.property("scale",&Transform::scale);
//}

Transform::Transform() :position(0.f), rotation(0.f), scale(1.0f) {
	Component::name = "Transform";

}
Transform::~Transform() {

}