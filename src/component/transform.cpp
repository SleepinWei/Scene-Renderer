#include"transform.h"
Transform::Transform() :position(0.f), rotation(0.f), scale(1.0f) {
	Component::name = "Transform";

}
Transform::~Transform() {

}