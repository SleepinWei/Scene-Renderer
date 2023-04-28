#include<utils/Reflection.h>

Reflection::Reflection()
{
}

Reflection::~Reflection()
{
	
}

Reflection::Reflection(const Reflection& _ref)
{

}

const Reflection& Reflection::operator=(const Reflection& _ref)
{
	// TODO: 在此处插入 return 语句
	return *this;
}

Reflection& Reflection::getInstance()
{
	static Reflection refInstance;
	return refInstance;
}

ClassMeta* Reflection::getMetaByClassName(string name)
{
	if (this->meta.find(name) == this->meta.end()) {
		return nullptr;
	}
	return meta[name];
}

void Reflection::addMeta(string className, ClassMeta* classMeta)
{
	meta.insert({ className,classMeta });
}
