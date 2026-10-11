#include "renderer/rhi/TreeGeometry.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <map>
#include <mutex>
#include <tuple>
#include <cmath>
namespace render {
namespace {
uint32_t hash(uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
struct Random {uint32_t state;float next(){state=hash(state+0x9e3779b9u);return float(state>>8)/16777216.f;}glm::vec3 vector(){return {next()*2-1,next()*2-1,next()*2-1};}};
struct Node {glm::vec3 p;int parent;float radius=0,path=0;uint32_t load=0;TreeWindVertex wind;};
struct Leaf {glm::vec3 p,axis,side;uint32_t parent=0;TreeWindVertex wind;};
struct Build {
    std::shared_ptr<MeshPayload> mesh=std::make_shared<MeshPayload>();
    void tri(glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec2 ua,glm::vec2 ub,glm::vec2 uc,TreeWindVertex wa={},TreeWindVertex wb={},TreeWindVertex wc={}){auto n=glm::cross(b-a,c-a);if(glm::dot(n,n)<1e-14f)return;n=glm::normalize(n);uint32_t i=uint32_t(mesh->vertices.size());mesh->vertices.insert(mesh->vertices.end(),{{a,n,ua},{b,n,ub},{c,n,uc}});mesh->treeWind.insert(mesh->treeWind.end(),{wa,wb,wc});mesh->indices.insert(mesh->indices.end(),{i,i+1,i+2});}
    void tube(const Node& a,const Node& b,int sides){auto axis=glm::normalize(b.p-a.p),u=glm::normalize(glm::cross(axis,std::abs(axis.y)<.9f?glm::vec3(0,1,0):glm::vec3(1,0,0))),v=glm::cross(axis,u);
        for(int j=0;j<sides;j++){float t=glm::two_pi<float>()*j/sides,q=glm::two_pi<float>()*(j+1)/sides;auto d=u*std::cos(t)+v*std::sin(t),e=u*std::cos(q)+v*std::sin(q);float x=float(j)/sides,y=float(j+1)/sides;auto nd=glm::normalize(d+axis*(a.radius-b.radius)/glm::length(b.p-a.p)),ne=glm::normalize(e+axis*(a.radius-b.radius)/glm::length(b.p-a.p));
            tri(a.p+d*a.radius,b.p+d*b.radius,a.p+e*a.radius,{x,a.path*8},{x,b.path*8},{y,a.path*8},a.wind,b.wind,a.wind);for(int c=0;c<3;c++)mesh->vertices[mesh->vertices.size()-3+c].normal=c==2?ne:nd;
            tri(a.p+e*a.radius,b.p+d*b.radius,b.p+e*b.radius,{y,a.path*8},{x,b.path*8},{y,b.path*8},a.wind,b.wind,b.wind);for(int c=0;c<3;c++)mesh->vertices[mesh->vertices.size()-3+c].normal=c==1?nd:ne;}}
    void leaf(const Leaf& l){auto mid=l.p+glm::normalize(glm::cross(l.axis,l.side))*glm::length(l.side)*.18f;
        // Six-edge blade follows the rounded alpha outline instead of clipping it to a diamond.
        const std::array<glm::vec2,6> outline={glm::vec2(.5f,0),{.08f,.28f},{.08f,.72f},{.5f,1},{.92f,.72f},{.92f,.28f}};
        for(size_t j=0;j<outline.size();j++){auto at=[&](glm::vec2 uv){return l.p+l.axis*(uv.y-.5f)+l.side*((uv.x-.5f)*2.f);};auto a=outline[j],b=outline[(j+1)%outline.size()];tri(at(a),at(b),mid,a,b,{.5f,.5f},l.wind,l.wind,l.wind);}}
};
std::array<TreeGeometry,2> generate(PlantType type,uint32_t seed){
    Random rng{seed};bool pine=type==PlantType::Conifer,shrub=type==PlantType::Shrub;
    std::vector<Node> nodes{{{0,0,0},-1}};std::vector<Leaf> leaves;
    auto add=[&](glm::vec3 p,int parent){float path=nodes[parent].path+glm::length(p-nodes[parent].p);nodes.push_back({p,parent,0,path,0});return int(nodes.size()-1);};
    int trunk=0;float bendX=(rng.next()-.5f)*.12f,bendZ=(rng.next()-.5f)*.10f;
    for(int j=1;j<=18;j++){float y=float(j)/18*(pine?1.f:shrub?.30f:.52f);trunk=add({bendX*y*y,y,bendZ*y*y},trunk);}
    if(pine){
        for(int tier=3;tier<18;tier++){int parent=tier;float y=nodes[parent].p.y,r=(1-y)*(.36f+.07f*rng.next());int limbs=7+int(rng.next()*4);
            for(int j=0;j<limbs;j++){float a=j*glm::two_pi<float>()/limbs+tier*1.73f+rng.next()*.25f;int at=parent;
                for(int k=1;k<=5;k++){float t=float(k)/5;auto p=nodes[parent].p+glm::vec3(std::cos(a)*r*t,-.055f*t+.055f*t*t,std::sin(a)*r*t);at=add(p,at);
                    for(int b=0;b<2;b++){auto direction=glm::vec3(std::cos(a+(b?1:-1)*.7f),.2f,std::sin(a+(b?1:-1)*.7f));int twig=add(p+direction*(.07f*(1-y)+.018f),at);
                        for(int n=0;n<6;n++){auto pos=glm::mix(nodes[at].p,nodes[twig].p,(n+.5f)/6);float angle=a+n*2.399963f;leaves.push_back({pos,{std::cos(angle)*.025f,.018f,std::sin(angle)*.025f},{-.004f*std::sin(angle),.001f,.004f*std::cos(angle)},uint32_t(twig)});nodes[twig].load++;}}
                }}
        }
    }else{
        // Crown attraction points guide a bounded static space-colonization skeleton.
        std::vector<glm::vec3> points;for(int j=0;j<1200;j++){auto p=rng.vector();if(glm::dot(p,p)>1){j--;continue;}float lobe=std::sin(p.y*8+seed*.01f)*.04f;
            points.push_back({p.x*(shrub?.38f:.40f)+lobe+bendX*.5f,(shrub?.57f:.69f)+p.y*(shrub?.40f:.30f),p.z*.34f+bendZ*.5f});}
        for(int step=0;step<100&&nodes.size()<900&&!points.empty();step++){
            std::vector<glm::vec3> sums(nodes.size(),glm::vec3(0));std::vector<uint32_t> count(nodes.size(),0);std::vector<glm::vec3> remaining;
            for(auto p:points){float best=.22f*.22f;int nearest=-1;bool killed=false;
                for(size_t i=1;i<nodes.size();i++){float r=glm::dot(p-nodes[i].p,p-nodes[i].p);if(r<.038f*.038f){killed=true;break;}if(r<best){best=r;nearest=int(i);}}
                if(killed)continue;remaining.push_back(p);if(nearest>=0){sums[nearest]+=glm::normalize(p-nodes[nearest].p);count[nearest]++;}}
            points=std::move(remaining);size_t before=nodes.size();
            for(size_t i=1;i<before&&nodes.size()<900;i++)if(count[i]){auto inertia=glm::normalize(nodes[i].p-nodes[nodes[i].parent].p);auto direction=glm::normalize(sums[i]/float(count[i])+inertia*.15f+glm::vec3(0,.025f,0));auto p=nodes[i].p+direction*.035f;
                bool duplicate=false;for(size_t k=before;k<nodes.size();k++)if(glm::length(nodes[k].p-p)<.015f){duplicate=true;break;}if(!duplicate)add(p,int(i));}
            if(before==nodes.size())break;
        }
        std::vector<uint32_t> children(nodes.size());for(size_t i=1;i<nodes.size();i++)children[nodes[i].parent]++;
        for(size_t i=18;i<nodes.size();i++)if(!children[i]||children[i]==1){int number=children[i]?6:24;auto shoot=glm::normalize(nodes[i].p-nodes[nodes[i].parent].p);
            for(int j=0;j<number;j++){auto p=nodes[i].p+rng.vector()*.024f+shoot*(rng.next()-.3f)*.025f;float a=rng.next()*glm::two_pi<float>();auto axis=glm::normalize(glm::vec3(std::cos(a),-.65f+rng.next()*1.25f,std::sin(a)))*(.023f+.012f*rng.next());auto side=glm::normalize(glm::cross(axis,glm::vec3(0,1,0)))*(.008f+.004f*rng.next());leaves.push_back({p,axis,side,uint32_t(i)});nodes[i].load++;}}
    }
    for(size_t i=nodes.size();i-->1;)nodes[nodes[i].parent].load+=std::max(1u,nodes[i].load);
    for(size_t i=0;i<nodes.size();i++)nodes[i].radius=std::max(.00065f,.00042f*std::sqrt(float(std::max(1u,nodes[i].load))));nodes[0].radius*=1.4f;
    // Normalize both LODs with the same full crown, preserving root and silhouette.
    float height=1;for(const auto& leaf:leaves)height=std::max(height,leaf.p.y+glm::length(leaf.axis)*.5f);
    std::vector<uint32_t> children(nodes.size());for(size_t i=1;i<nodes.size();i++)children[nodes[i].parent]++;
    std::vector<int> limbs(nodes.size(),-1),twigs(nodes.size(),-1);
    for(size_t i=19;i<nodes.size();i++){int parent=nodes[i].parent;limbs[i]=parent<=18?int(i):limbs[parent];twigs[i]=parent>18&&children[parent]>1&&twigs[parent]<0?int(i):twigs[parent];
        if(limbs[i]>=0)nodes[i].wind.branch=glm::vec4(nodes[nodes[limbs[i]].parent].p/height,pine?.6f:1.f);
        if(twigs[i]>=0)nodes[i].wind.twig=glm::vec4(nodes[nodes[twigs[i]].parent].p/height,1.f);}
    for(auto& leaf:leaves){leaf.wind=nodes[leaf.parent].wind;leaf.wind.leaf=glm::vec4(leaf.p/height,pine?.35f:1.f);}
    std::array<TreeGeometry,2> result;
    for(uint32_t lod=0;lod<2;lod++){
        Build bark,foliage;for(size_t i=1;i<nodes.size();i++)if(!lod||nodes[i].radius>.0045f)bark.tube(nodes[nodes[i].parent],nodes[i],lod?4:7);
        if(!lod)for(auto leaf:leaves)foliage.leaf(leaf);
        else {
            // Aggregate nearby leaves into folded foliage patches with a cluster alpha map.
            struct Cluster {glm::vec3 p{0},normal{0};float area=0;uint32_t count=0;TreeWindVertex wind;};std::map<std::tuple<int,int,int>,Cluster> clusters;
            float cell=pine?.065f:.075f;for(auto l:leaves){auto id=glm::ivec3(glm::floor(l.p/cell));auto& c=clusters[{id.x,id.y,id.z}];if(!c.count)c.wind=l.wind;c.p+=l.p;c.normal+=glm::normalize(glm::cross(l.axis,l.side));c.area+=glm::length(l.axis)*glm::length(l.side);c.count++;}
            for(auto& pair:clusters){auto c=pair.second;auto p=c.p/float(c.count);auto n=glm::length(c.normal)>.01f?glm::normalize(c.normal):glm::vec3(0,1,0);auto u=glm::normalize(glm::cross(n,std::abs(n.y)<.9f?glm::vec3(0,1,0):glm::vec3(1,0,0))),v=glm::cross(n,u);float size=std::clamp(std::sqrt(c.area)*1.5f,.025f,.105f);c.wind.leaf=glm::vec4(p/height,.12f);foliage.leaf({p,u*size*2.f,v*size*.65f,0,c.wind});}
        }
        for(auto mesh:{bark.mesh,foliage.mesh})for(auto& vertex:mesh->vertices)vertex.position/=height;
        result[lod]={bark.mesh,foliage.mesh,uint32_t(nodes.size()-1),uint32_t(leaves.size())};
    }return result;
}
ImageRGBA8 image(uint32_t w,uint32_t h){return {w,h,std::vector<uint8_t>(size_t(w)*h*4)};}
uint8_t byte(float v){return uint8_t(std::clamp(std::lround(v*255),0l,255l));}
}
TreeGeometry treeGeometry(PlantType type,uint32_t lod,uint32_t seed){
    if((type!=PlantType::Broadleaf&&type!=PlantType::Conifer&&type!=PlantType::Shrub)||lod>1)throw std::invalid_argument("Unknown tree geometry");
    static std::mutex mutex;static std::map<std::pair<PlantType,uint32_t>,std::array<TreeGeometry,2>> cache;
    std::lock_guard<std::mutex> lock(mutex);auto key=std::make_pair(type,seed);auto found=cache.find(key);if(found==cache.end())found=cache.emplace(key,generate(type,seed)).first;return found->second[lod];
}
MaterialDesc treeMaterial(PlantType type,bool foliage,uint32_t lod){
    static std::mutex mutex;static std::map<std::tuple<PlantType,bool,uint32_t>,MaterialDesc> cache;std::lock_guard<std::mutex> lock(mutex);auto key=std::make_tuple(type,foliage,lod);auto found=cache.find(key);if(found!=cache.end())return found->second;
    constexpr uint32_t n=128;auto albedo=image(n,n),normal=image(n,n),rough=image(n,n),special=image(n,n);bool pine=type==PlantType::Conifer;
    for(uint32_t y=0;y<n;y++)for(uint32_t x=0;x<n;x++){
        float u=(x+.5f)/n,v=(y+.5f)/n,shade=1,alpha=1,transmission=1;glm::vec3 rgb,gradient;
        if(foliage){float vein=std::exp(-std::abs(u-.5f)*90),sideVein=std::pow(.5f+.5f*std::cos((v-std::abs(u-.5f)*.7f)*80),18.f);shade=.85f+.12f*v-.20f*vein-.06f*sideVein;
            rgb=(pine?glm::vec3(.20f,.37f,.17f):glm::vec3(.32f,.48f,.12f))*shade;gradient={.10f*std::sin(u*40)+.035f*std::sin(v*80),.04f*std::sin(v*80),1};transmission=.9f-.45f*vein-.15f*sideVein;
            if(!lod){float width=.48f*std::sin(glm::pi<float>()*v);alpha=glm::smoothstep(-.015f,.015f,width-std::abs(u-.5f));}
            else {alpha=0;float best=0;for(int j=0;j<13;j++){float a=j*2.399963f,cx=.5f+.31f*std::cos(a)*std::sqrt((j+.5f)/13),cy=.5f+.31f*std::sin(a)*std::sqrt((j+.5f)/13);auto d=glm::vec2(u-cx,v-cy);float r=std::pow((d.x*std::cos(a)+d.y*std::sin(a))/.16f,2.f)+std::pow((-d.x*std::sin(a)+d.y*std::cos(a))/.08f,2.f);best=std::max(best,1-r);}alpha=glm::smoothstep(-.1f,.1f,best);}
        }else{float ridge=.5f+.5f*std::sin(u*glm::two_pi<float>()*12+1.2f*std::sin(v*glm::two_pi<float>()*2));float detail=(float(hash(x*65537u+y*104729u)&255)/255.f-.5f)*.08f;shade=.72f+.25f*ridge+detail;rgb=glm::vec3(.40f,.30f,.20f)*shade;gradient={.22f*std::cos(u*glm::two_pi<float>()*12+1.2f*std::sin(v*glm::two_pi<float>()*2)),.025f*std::sin(v*glm::two_pi<float>()*2),1};}
        gradient=glm::normalize(gradient);size_t at=(size_t(y)*n+x)*4;for(int c=0;c<3;c++){albedo.pixels[at+c]=byte(rgb[c]);normal.pixels[at+c]=byte(gradient[c]*.5f+.5f);rough.pixels[at+c]=byte(foliage?.68f+.12f*(1-shade):.92f);special.pixels[at+c]=255;}albedo.pixels[at+3]=byte(alpha);normal.pixels[at+3]=rough.pixels[at+3]=255;special.pixels[at+3]=byte(transmission);
    }
    MaterialDesc d;d.parameters.factors={0,1,1,foliage?.45f:0};d.parameters.emissiveNormal.w=foliage?.35f:1;
    d.sharedImages[0]=std::make_shared<ImageRGBA8>(std::move(albedo));d.sharedImages[1]=std::make_shared<ImageRGBA8>(std::move(normal));d.sharedImages[3]=std::make_shared<ImageRGBA8>(std::move(rough));d.sharedSpecial=std::make_shared<ImageRGBA8>(std::move(special));
    if(foliage){d.extension.settings.w=1;d.extension.features.x|=32;d.extension.lobes.w=.55f;}cache[key]=d;return d;
}
}
