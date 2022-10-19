#include"../renderer/Mesh_Filter.h"
#include"../renderer/Mesh_Renderer.h"
#include"../component/transform.h"
#include"meta_register.h"
//#include<UDRefl/UDRefl.hpp>
#include<glm/glm.hpp>
#include<memory>
//using namespace Ubpa;
//using namespace Ubpa::UDRefl;

//using Ubpa::UDRefl::Mngr;
//using Ubpa::UDRefl::SharedObject;

MetaRegister::MetaRegister() {
	// Transform 
	//Mngr.RegisterType<Transform>();
	//Mngr.AddField<&Transform::position>("position");
	//Mngr.AddField<&Transform::rotation>("rotation");
	//Mngr.AddField<&Transform::scale>("scale");
}

MetaRegister::~MetaRegister() {

}

void MetaRegister::test() {
	//SharedObject v = Mngr.MakeShared(Ubpa::Type_of<Transform>);
	//std::shared_ptr<Transform> p = std::static_pointer_cast<Transform>(v.GetBuffer());
	//std::cout << v.GetType().GetName() << '\n';
	//std::cout << p.get() << '\n';
}