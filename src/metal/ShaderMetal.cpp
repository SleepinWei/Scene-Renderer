#include "utils/Shader.h"
#include "metal/MetalBackend.h"
Shader::Shader(const char* v,const char* f,const char* g,const char* c,const char* e) {
    requireMat=true; ID=MetalBackend::createProgram(v,f,g,c,e); linked=true;
}
Shader::Shader(const char* c) { requireMat=false; ID=MetalBackend::createComputeProgram(c); linked=true; }
void Shader::use() { MetalBackend::useProgram(ID); }
void Shader::setBool(const std::string& n,bool v)const { int i=v; setInt(n,i); }
void Shader::setInt(const std::string& n,int v)const { MetalBackend::setUniform(ID,n.c_str(),&v,1); }
void Shader::setUInt(const std::string& n,unsigned v)const { MetalBackend::setUniform(ID,n.c_str(),&v,1); }
void Shader::setFloat(const std::string& n,float v)const { MetalBackend::setUniform(ID,n.c_str(),&v,1); }
void Shader::setVec2(const std::string& n,const glm::vec2& v)const { MetalBackend::setUniform(ID,n.c_str(),&v[0],2); }
void Shader::setVec2(const std::string& n,float x,float y)const {setVec2(n,glm::vec2(x,y));}
void Shader::setVec3(const std::string& n,const glm::vec3& v)const { MetalBackend::setUniform(ID,n.c_str(),&v[0],3); }
void Shader::setVec3(const std::string& n,float x,float y,float z)const {setVec3(n,glm::vec3(x,y,z));}
void Shader::setVec4(const std::string& n,const glm::vec4& v)const { MetalBackend::setUniform(ID,n.c_str(),&v[0],4); }
void Shader::setVec4(const std::string& n,float x,float y,float z,float w) {setVec4(n,glm::vec4(x,y,z,w));}
void Shader::setMat2(const std::string& n,const glm::mat2& v)const { MetalBackend::setUniform(ID,n.c_str(),&v[0][0],2,2); }
void Shader::setMat3(const std::string& n,const glm::mat3& v)const { MetalBackend::setUniform(ID,n.c_str(),&v[0][0],3,3); }
void Shader::setMat4(const std::string& n,const glm::mat4& v)const { MetalBackend::setUniform(ID,n.c_str(),&v[0][0],4,4); }
void Shader::setUniformBuffer(const std::string& n,int b)const {MetalBackend::setBlockBinding(ID,n.c_str(),b);}
