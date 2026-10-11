#pragma once
#include <glm/glm.hpp>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <initializer_list>
namespace render {
// Normalized local pivots: primary limb, secondary twig, individual leaf.
struct TreeWindVertex {glm::vec4 branch{0},twig{0},leaf{0};};
struct TreeWindSettings {
    bool enabled=true;
    glm::vec2 velocity{6,2};
    float phase=0,branches=1,flutter=1;
    void validate()const{for(float x:{velocity.x,velocity.y,phase,branches,flutter})if(!std::isfinite(x))throw std::invalid_argument("Nonfinite tree wind");if(glm::length(velocity)>100||branches<0||branches>2||flutter<0||flutter>2)throw std::invalid_argument("Tree wind outside domain");}
};
inline glm::vec3 treeRotate(glm::vec3 p,glm::vec3 axis,float a){return p*std::cos(a)+glm::cross(axis,p)*std::sin(a)+axis*glm::dot(axis,p)*(1-std::cos(a));}
inline void treeWindDeform(glm::vec3& p,glm::vec3& n,const TreeWindVertex& v,const TreeWindSettings& s,float time){
    float speed=glm::length(s.velocity);if(!s.enabled||speed<1e-6f)return;
    glm::vec2 direction=s.velocity/speed;glm::vec3 axis(direction.y,0,-direction.x);
    float strength=speed/(10+speed),phase=s.phase;
    float a=.12f*strength*s.branches*v.branch.w*std::sin(time*.85f+phase+glm::dot(glm::vec3(v.branch),glm::vec3(17,7,13)));
    float b=.20f*strength*s.branches*v.twig.w*std::sin(time*1.9f+phase+glm::dot(glm::vec3(v.twig),glm::vec3(31,19,23)));
    float c=.55f*strength*s.flutter*v.leaf.w*std::sin(time*7.3f+phase+glm::dot(glm::vec3(v.leaf),glm::vec3(131,77,113)));
    if(v.leaf.w>0){auto leafAxis=glm::normalize(glm::vec3(1,.3f,.6f));p=glm::vec3(v.leaf)+treeRotate(p-glm::vec3(v.leaf),leafAxis,c);n=treeRotate(n,leafAxis,c);}
    p=glm::vec3(v.twig)+treeRotate(p-glm::vec3(v.twig),axis,b);n=treeRotate(n,axis,b);
    p=glm::vec3(v.branch)+treeRotate(p-glm::vec3(v.branch),axis,a);n=treeRotate(n,axis,a);
    float sway=.035f*strength*s.branches*(.7f+.3f*std::sin(time*.65f+phase));
    float y=std::max(p.y,0.f);p.x+=direction.x*sway*y*y;p.z+=direction.y*sway*y*y;
    n.y-=2*y*sway*(direction.x*n.x+direction.y*n.z);n=glm::normalize(n);
}
}
