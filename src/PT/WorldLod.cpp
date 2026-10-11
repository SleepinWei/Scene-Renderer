#include "PT/WorldLod.h"
#include "PT/CpuPathTracer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <set>
#include <mutex>
#include <stdexcept>
#include <json/json.hpp>
namespace pt {
struct WorldLodCache::Impl {
    struct Entry {std::shared_ptr<const render::MeshPayload> source,low;WorldLodSettings settings;MeshLodReport report;std::string protection;uint64_t revision=0,used=0;size_t bytes=0;};
    std::mutex mutex;std::map<const render::MeshPayload *,Entry> entries;size_t budget,bytes=0;uint64_t clock=0;
    explicit Impl(size_t b):budget(b){}
};
WorldLodCache::WorldLodCache(size_t budget):impl_(std::make_unique<Impl>(budget)){}
WorldLodCache::~WorldLodCache()=default;
void WorldLodSettings::validate() const {
    if(!std::isfinite(maxError)||maxError<=0||maxError>100||!std::isfinite(targetRatio)||targetRatio<=0||targetRatio>1||!std::isfinite(maxUvError)||maxUvError<0||maxUvError>1||levels<1||levels>12)throw std::invalid_argument("PT world LOD settings outside domain");
}
namespace {
struct Union {
    std::vector<uint32_t> p;
    explicit Union(size_t n):p(n){std::iota(p.begin(),p.end(),0);}
    uint32_t root(uint32_t i){while(p[i]!=i){p[i]=p[p[i]];i=p[i];}return i;}
    void join(uint32_t a,uint32_t b){a=root(a);b=root(b);if(a!=b)p[std::max(a,b)]=std::min(a,b);}
};
uint32_t bits(float f){if(f==0)f=0;uint32_t v;std::memcpy(&v,&f,4);return v;}
using Attribute=std::array<uint32_t,12>;
Attribute attribute(const render::MeshVertex &v,glm::vec4 t){return {bits(v.position.x),bits(v.position.y),bits(v.position.z),bits(v.normal.x),bits(v.normal.y),bits(v.normal.z),bits(v.uv.x),bits(v.uv.y),bits(t.x),bits(t.y),bits(t.z),bits(t.w)};}
using Edge=std::pair<uint32_t,uint32_t>;
Edge edge(uint32_t a,uint32_t b){return std::minmax(a,b);}
struct Prepared {
    render::MeshPayload mesh;
    std::vector<bool> locked;
    std::map<Edge,uint32_t> edges;
    bool safe=true;
};
Prepared prepare(const render::MeshPayload &source){
    Prepared p;p.mesh.id=source.id;p.mesh.revision=source.revision;
    if(source.indices.size()%3||(!source.pathTracingTangents.empty()&&source.pathTracingTangents.size()!=source.vertices.size()))throw std::invalid_argument("PT LOD invalid mesh layout");
    std::map<Attribute,uint32_t> unique;std::vector<uint32_t> map(source.vertices.size());
    std::map<std::array<uint32_t,3>,std::vector<uint32_t>> positions;
    for(size_t i=0;i<source.vertices.size();++i){const auto &v=source.vertices[i];for(float f:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y})if(!std::isfinite(f))throw std::invalid_argument("PT LOD nonfinite vertex");
        const auto t=source.pathTracingTangents.empty()?glm::vec4(0):source.pathTracingTangents[i];auto key=attribute(v,t);
        auto it=unique.emplace(key,uint32_t(p.mesh.vertices.size()));map[i]=it.first->second;
        if(it.second){p.mesh.vertices.push_back(v);if(!source.pathTracingTangents.empty())p.mesh.pathTracingTangents.push_back(t);positions[{key[0],key[1],key[2]}].push_back(map[i]);}
    }
    p.locked.resize(p.mesh.vertices.size(),false);
    for(auto index:source.indices){if(index>=map.size())throw std::invalid_argument("PT LOD index outside mesh");p.mesh.indices.push_back(map[index]);}
    for(size_t i=0;i<p.mesh.indices.size();i+=3){auto a=p.mesh.indices[i],b=p.mesh.indices[i+1],c=p.mesh.indices[i+2];if(a==b||b==c||a==c){p.safe=false;continue;}for(auto e:{edge(a,b),edge(b,c),edge(c,a)})++p.edges[e];}
    for(auto e:p.edges)if(e.second!=2){p.locked[e.first.first]=p.locked[e.first.second]=true;if(e.second>2)p.safe=false;}
    for(auto &v:positions)if(v.second.size()>1)for(auto i:v.second)p.locked[i]=true;
    return p;
}
struct Candidate {std::shared_ptr<render::MeshPayload> mesh;MeshLodLevel level;};
Candidate cluster(const Prepared &p,float cell,float scale,const WorldLodSettings &s){
    const auto &m=p.mesh;Union groups(m.vertices.size());
    glm::vec3 low(INFINITY);for(auto v:m.vertices)low=glm::min(low,v.position);
    auto key=[&](uint32_t i){const auto &v=m.vertices[i];const auto q=glm::floor((v.position-low)/cell);return std::array<int64_t,6>{int64_t(q.x),int64_t(q.y),int64_t(q.z),int64_t(std::floor(v.normal.x*8)),int64_t(std::floor(v.normal.y*8)),int64_t(std::floor(v.normal.z*8))};};
    // Greedy contractions within each spatial cluster. Check the manifold
    // link condition and affected face orientations before every contraction,
    // so a single unsafe corner does not discard an entire coarse mesh level.
    std::vector<std::set<uint32_t>> incident(m.vertices.size());
    std::vector<std::vector<uint32_t>> members(m.vertices.size());
    std::vector<render::MeshVertex> representative=m.vertices;
    for(uint32_t i=0;i<m.vertices.size();++i)members[i].push_back(i);
    for(uint32_t t=0;t<m.indices.size()/3;++t)for(uint32_t c=0;c<3;++c)incident[m.indices[t*3+c]].insert(t);
    auto roots=[&](uint32_t t){return std::array<uint32_t,3>{groups.root(m.indices[t*3]),groups.root(m.indices[t*3+1]),groups.root(m.indices[t*3+2])};};
    auto active=[](const std::array<uint32_t,3> &f){return f[0]!=f[1]&&f[1]!=f[2]&&f[0]!=f[2];};
    for(auto e:p.edges){auto a=groups.root(e.first.first),b=groups.root(e.first.second);if(a==b||p.locked[a]||p.locked[b]||key(a)!=key(b))continue;
        std::set<uint32_t> neighborsA,neighborsB,opposite;uint32_t sharedFaces=0;
        for(auto t:incident[a]){auto f=roots(t);if(!active(f))continue;for(auto v:f)if(v!=a)neighborsA.insert(v);
            if(std::find(f.begin(),f.end(),b)!=f.end()){++sharedFaces;for(auto v:f)if(v!=a&&v!=b)opposite.insert(v);}}
        if(sharedFaces!=2)continue;
        for(auto t:incident[b]){auto f=roots(t);if(active(f))for(auto v:f)if(v!=b)neighborsB.insert(v);}
        std::set<uint32_t> shared;std::set_intersection(neighborsA.begin(),neighborsA.end(),neighborsB.begin(),neighborsB.end(),std::inserter(shared,shared.begin()));
        if(shared!=opposite)continue;
        const float A=float(members[a].size()),B=float(members[b].size());auto va=representative[a],vb=representative[b];render::MeshVertex candidate;
        candidate.position=(va.position*A+vb.position*B)/(A+B);candidate.uv=(va.uv*A+vb.uv*B)/(A+B);
        candidate.normal=glm::vec3(0);for(auto root:{a,b})for(auto v:members[root])candidate.normal+=m.vertices[v].normal;
        if(glm::length(candidate.normal)<1e-8f)continue;candidate.normal=glm::normalize(candidate.normal);bool safe=true;
        for(auto root:{a,b})for(auto v:members[root])if(glm::distance(m.vertices[v].position,candidate.position)*scale>s.maxError||glm::distance(m.vertices[v].uv,candidate.uv)>s.maxUvError||glm::dot(glm::normalize(m.vertices[v].normal),candidate.normal)<.94f)safe=false;
        if(!safe)continue;
        std::set<uint32_t> affected=incident[a];affected.insert(incident[b].begin(),incident[b].end());
        for(auto t:affected){auto f=roots(t);if(!active(f))continue;if(std::find(f.begin(),f.end(),a)!=f.end()&&std::find(f.begin(),f.end(),b)!=f.end())continue;
            std::array<glm::vec3,3> before,after;for(uint32_t i=0;i<3;++i){before[i]=representative[f[i]].position;after[i]=f[i]==a||f[i]==b?candidate.position:before[i];}
            auto oldN=glm::cross(before[1]-before[0],before[2]-before[0]),newN=glm::cross(after[1]-after[0],after[2]-after[0]);
            auto sourceN=glm::cross(m.vertices[m.indices[t*3+1]].position-m.vertices[m.indices[t*3]].position,m.vertices[m.indices[t*3+2]].position-m.vertices[m.indices[t*3]].position);
            if(glm::length(newN)<1e-12f||glm::dot(oldN,newN)<=0||glm::dot(sourceN,newN)<=0){safe=false;break;}}
        if(!safe)continue;
        auto keep=std::min(a,b),drop=std::max(a,b);groups.join(a,b);representative[keep]=candidate;
        members[keep].insert(members[keep].end(),members[drop].begin(),members[drop].end());members[drop].clear();incident[keep]=std::move(affected);incident[drop].clear();
    }
    auto out=std::make_shared<render::MeshPayload>();out->id=m.id;out->revision=m.revision;
    std::map<uint32_t,uint32_t> ids;std::vector<uint32_t> remap(m.vertices.size()),count;
    for(uint32_t i=0;i<m.vertices.size();++i){auto id=ids.emplace(groups.root(i),uint32_t(ids.size()));remap[i]=id.first->second;
        if(id.second){out->vertices.push_back({glm::vec3(0),glm::vec3(0),glm::vec2(0)});count.push_back(0);if(!m.pathTracingTangents.empty())out->pathTracingTangents.emplace_back(0);}
        auto &v=out->vertices[remap[i]];v.position+=m.vertices[i].position;v.normal+=m.vertices[i].normal;v.uv+=m.vertices[i].uv;++count[remap[i]];
        if(!m.pathTracingTangents.empty())out->pathTracingTangents[remap[i]]+=m.pathTracingTangents[i];
    }
    for(size_t i=0;i<out->vertices.size();++i){auto &v=out->vertices[i];v.position/=float(count[i]);v.uv/=float(count[i]);if(glm::length(v.normal)<1e-8f)return {};v.normal=glm::normalize(v.normal);
        if(!m.pathTracingTangents.empty()){auto &t=out->pathTracingTangents[i];auto xyz=glm::vec3(t)-v.normal*glm::dot(v.normal,glm::vec3(t));if(glm::length(xyz)<1e-8f)return {};t=glm::vec4(glm::normalize(xyz),t.w>=0?1.f:-1.f);}}
    MeshLodLevel level;level.cellSize=cell;
    for(size_t i=0;i<m.vertices.size();++i){const auto &a=m.vertices[i],&b=out->vertices[remap[i]];level.maxDisplacement=std::max(level.maxDisplacement,glm::distance(a.position,b.position)*scale);level.maxUvDisplacement=std::max(level.maxUvDisplacement,glm::distance(a.uv,b.uv));
        if(glm::dot(glm::normalize(a.normal),b.normal)<.94f)return {};}
    if(level.maxDisplacement>s.maxError||level.maxUvDisplacement>s.maxUvError)return {};
    std::set<std::array<uint32_t,3>> faces;std::map<Edge,uint32_t> edges;
    for(size_t i=0;i<m.indices.size();i+=3){auto a=m.indices[i],b=m.indices[i+1],c=m.indices[i+2];auto A=remap[a],B=remap[b],C=remap[c];if(A==B||B==C||A==C)continue;
        auto oldN=glm::cross(m.vertices[b].position-m.vertices[a].position,m.vertices[c].position-m.vertices[a].position),newN=glm::cross(out->vertices[B].position-out->vertices[A].position,out->vertices[C].position-out->vertices[A].position);
        if(glm::length(newN)<1e-12f||glm::dot(oldN,newN)<=0)return {};
        std::array<uint32_t,3> f{A,B,C};auto sorted=f;std::sort(sorted.begin(),sorted.end());if(!faces.insert(sorted).second)return {};
        out->indices.insert(out->indices.end(),f.begin(),f.end());for(auto e:{edge(A,B),edge(B,C),edge(C,A)})++edges[e];
    }
    // Preserve every original boundary and reject new holes/nonmanifold edges.
    std::set<Edge> boundary;for(auto e:p.edges)if(e.second==1)boundary.insert(edge(remap[e.first.first],remap[e.first.second]));
    std::set<Edge> nextBoundary;for(auto e:edges){if(e.second>2)return {};if(e.second==1)nextBoundary.insert(e.first);}if(nextBoundary!=boundary||out->indices.empty())return {};
    level.triangles=out->indices.size()/3;level.vertices=out->vertices.size();return {out,level};
}
std::string protectedMaterial(const render::SnapshotDraw &d){
    if(d.pathTracingIor>0||d.pathTracingKind>=2||d.pathTracingBsdfModel==5||glm::length(d.pathTracingAbsorption)>0||glm::length(d.pathTracingScattering)>0)return "optical-boundary-or-procedural";
    if(d.parameters.albedoAlpha.w<1||d.parameters.factors.w>0||d.extension.settings.w>0)return "alpha-or-thin-surface";
    if(glm::length(glm::vec3(d.parameters.emissiveNormal))>0||d.extension.settings.z>0)return "emitter";
    return {};
}
}
render::RenderWorldSnapshot buildLowLodWorld(const render::RenderWorldSnapshot &world,const WorldLodSettings &s,WorldLodReport *report,std::shared_ptr<WorldLodCache> cache){
    s.validate();auto started=std::chrono::steady_clock::now();auto out=world;WorldLodReport r;
    struct Group {std::shared_ptr<const render::MeshPayload> source;std::vector<size_t> draws;float scale=0;std::string protect;};std::map<const render::MeshPayload *,Group> groups;
    for(size_t i=0;i<world.draws.size();++i){const auto &d=world.draws[i];if(!d.mesh)continue;auto &g=groups[d.mesh.get()];g.source=d.mesh;g.draws.push_back(i);
        // sqrt(||M||1 * ||M||inf) bounds spectral norm; handles shear and scale.
        float one=0,inf=0;for(int a=0;a<3;++a){float col=0,row=0;for(int b=0;b<3;++b){col+=std::abs(d.model[a][b]);row+=std::abs(d.model[b][a]);}one=std::max(one,col);inf=std::max(inf,row);}g.scale=std::max(g.scale,std::sqrt(one*inf));
        auto reason=protectedMaterial(d);if(!reason.empty())g.protect=reason;
    }
    for(auto &entry:groups){auto &g=entry.second;auto source=g.source;MeshLodReport m;m.sourceId=source->id;m.sourceTriangles=source->indices.size()/3;m.selectedTriangles=m.sourceTriangles;m.instances=g.draws.size();m.worldScaleBound=g.scale;m.preservation=g.protect;
        m.levels.push_back({m.sourceTriangles,source->vertices.size(),0,0,0});std::shared_ptr<const render::MeshPayload> best=source;
        std::unique_lock<std::mutex> cacheLock;if(cache)cacheLock=std::unique_lock<std::mutex>(cache->impl_->mutex);
        bool cached=false;
        if(cache){auto &c=*cache->impl_;auto found=c.entries.find(source.get());if(found!=c.entries.end()){auto &e=found->second;const auto &a=e.settings;
            if(e.revision==source->revision&&e.report.worldScaleBound==g.scale&&a.enabled==s.enabled&&a.maxError==s.maxError&&a.maxUvError==s.maxUvError&&a.targetRatio==s.targetRatio&&a.levels==s.levels&&e.protection==g.protect){
                m=e.report;m.instances=g.draws.size();best=e.low;e.used=++c.clock;cached=true;++r.cacheHits;}}}
        if(!cached&&s.enabled&&g.protect.empty()&&m.sourceTriangles>=32&&g.scale>1e-12f){auto p=prepare(*source);if(!p.safe)m.preservation="nonmanifold-or-degenerate";else{
            for(uint32_t level=0;level<s.levels;++level){float cell=s.maxError/g.scale/std::sqrt(3.f)*std::pow(2.f,float(level)-3.f);auto candidate=cluster(p,cell,g.scale,s);
                if(!candidate.mesh||candidate.level.triangles>=m.selectedTriangles)continue;m.levels.push_back(candidate.level);m.selectedLevel=m.levels.size()-1;m.selectedTriangles=candidate.level.triangles;best=candidate.mesh;
                if(double(m.selectedTriangles)/m.sourceTriangles<=s.targetRatio)break;
            }
        }}
        if(cache&&!cached){auto &c=*cache->impl_;size_t bytes=source->vertices.capacity()*sizeof(render::MeshVertex)+source->indices.capacity()*4+source->pathTracingTangents.capacity()*16;
            if(best!=source)bytes+=best->vertices.capacity()*sizeof(render::MeshVertex)+best->indices.capacity()*4+best->pathTracingTangents.capacity()*16;
            auto previous=c.entries.find(source.get());if(previous!=c.entries.end()){c.bytes-=previous->second.bytes;c.entries.erase(previous);}
            if(bytes<=c.budget){while(!c.entries.empty()&&(c.entries.size()>=32||bytes>c.budget-c.bytes)){auto oldest=std::min_element(c.entries.begin(),c.entries.end(),[](auto &a,auto &b){return a.second.used<b.second.used;});c.bytes-=oldest->second.bytes;c.entries.erase(oldest);}
                c.entries[source.get()]={source,best,s,m,g.protect,source->revision,++c.clock,bytes};c.bytes+=bytes;}}
        if(cache)r.retainedCacheBytes=cache->impl_->bytes;
        for(auto i:g.draws)out.draws[i].mesh=best;
        r.sourceTriangles+=m.sourceTriangles*m.instances;r.selectedTriangles+=m.selectedTriangles*m.instances;r.sourceUniqueTriangles+=m.sourceTriangles;r.selectedUniqueTriangles+=m.selectedTriangles;r.meshes.push_back(std::move(m));
    }
    r.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();if(report)*report=std::move(r);return out;
}
void writeWorldLodReport(const WorldLodReport &r,const WorldLodSettings &s,const std::string &path){
    nlohmann::json j={{"algorithm","topology-constrained spatial vertex clustering"},{"enabled",s.enabled},{"max_world_vertex_displacement",s.maxError},{"max_uv_displacement",s.maxUvError},{"target_ratio",s.targetRatio},{"build_seconds",r.seconds},{"cache_hits",r.cacheHits},{"retained_cache_bytes",r.retainedCacheBytes},{"source_triangles",r.sourceTriangles},{"selected_triangles",r.selectedTriangles},{"source_unique_triangles",r.sourceUniqueTriangles},{"selected_unique_triangles",r.selectedUniqueTriangles},{"meshes",nlohmann::json::array()}};
    for(auto &m:r.meshes){nlohmann::json levels=nlohmann::json::array();for(auto &l:m.levels)levels.push_back({{"triangles",l.triangles},{"vertices",l.vertices},{"cell_size_local",l.cellSize},{"max_world_vertex_displacement",l.maxDisplacement},{"max_uv_displacement",l.maxUvDisplacement}});
        j["meshes"].push_back({{"source_id",m.sourceId},{"instances",m.instances},{"selected_level",m.selectedLevel},{"preservation",m.preservation},{"world_scale_bound",m.worldScaleBound},{"levels",levels}});}
    std::ofstream file(path);file<<j.dump(2)<<'\n';if(!file)throw std::runtime_error("Cannot save PT world LOD report");
}
void validateWorldLod(){
    auto check=[](bool v,const char *message){if(!v)throw std::runtime_error(message);};
    render::RenderWorldSnapshot w;auto mesh=std::make_shared<render::MeshPayload>();mesh->id=12;constexpr uint32_t n=40;
    for(uint32_t y=0;y<=n;++y)for(uint32_t x=0;x<=n;++x)mesh->vertices.push_back({{float(x)/n*4,0,float(y)/n*4},{0,1,0},{float(x)/n,float(y)/n}});
    for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x){if(x>=17&&x<23&&y>=17&&y<23)continue;auto a=y*(n+1)+x,b=a+1,c=a+n+1,d=c+1;mesh->indices.insert(mesh->indices.end(),{a,c,b,b,c,d});}
    render::SnapshotDraw draw;draw.mesh=mesh;draw.parameters.factors={0,.7f,1,0};w.draws.push_back(draw);w.draws.push_back(draw);w.draws.back().model[0][0]=2;
    WorldLodSettings s;s.enabled=true;s.maxError=.35f;s.maxUvError=.1f;s.targetRatio=.5f;WorldLodReport r;
    auto low=buildLowLodWorld(w,s,&r);check(r.selectedUniqueTriangles<r.sourceUniqueTriangles*.8,"Clustering did not reduce a tessellated surface");check(low.draws[0].mesh==low.draws[1].mesh,"Shared mesh LOD rebuilt per instance");check(w.draws[0].mesh==mesh&&mesh->indices.size()/3==3128,"LOD mutated the full world");
    const auto &chosen=r.meshes[0].levels[r.meshes[0].selectedLevel];check(chosen.maxDisplacement<=s.maxError&&chosen.maxUvDisplacement<=s.maxUvError,"LOD exceeded error budget under instance scale");
    auto again=buildLowLodWorld(w,s);check(again.draws[0].mesh->indices==low.draws[0].mesh->indices&&again.draws[0].mesh->vertices.size()==low.draws[0].mesh->vertices.size(),"Clustering is nondeterministic");
    auto cache=std::make_shared<WorldLodCache>();auto cold=buildLowLodWorld(w,s,nullptr,cache);WorldLodReport hotReport;
    w.draws[0].parameters.albedoAlpha.x=.2f;auto hot=buildLowLodWorld(w,s,&hotReport,cache);check(hotReport.cacheHits==1&&hot.draws[0].mesh==cold.draws[0].mesh,"Material edit rebuilt cached LOD / BLAS identity");
    auto tiny=std::make_shared<WorldLodCache>(1);buildLowLodWorld(w,s,nullptr,tiny);buildLowLodWorld(w,s,&hotReport,tiny);check(hotReport.cacheHits==0&&hotReport.retainedCacheBytes==0,"LOD retention exceeded its byte budget");
    auto original=prepare(*mesh),simplified=prepare(*low.draws[0].mesh);std::set<std::array<uint32_t,3>> a,b;
    for(auto e:original.edges)if(e.second==1)for(auto i:{e.first.first,e.first.second}){auto p=original.mesh.vertices[i].position;a.insert({bits(p.x),bits(p.y),bits(p.z)});}
    for(auto e:simplified.edges)if(e.second==1)for(auto i:{e.first.first,e.first.second}){auto p=simplified.mesh.vertices[i].position;b.insert({bits(p.x),bits(p.y),bits(p.z)});}
    check(a==b,"Clustering changed hole / outer boundaries");
    render::RenderWorldSnapshot fullWorld=w,lowWorld=low;fullWorld.draws.resize(1);lowWorld.draws.resize(1);
    CpuScene fullScene(fullWorld),lowScene(lowWorld);
    for(uint32_t i=0;i<256;++i){glm::vec3 origin{float((i*71)%257)/257.f*4,2,float((i*113)%263)/263.f*4};Surface fullHit,lowHit;
        bool f=fullScene.intersect(origin,{0,-1,0},.0001f,10,fullHit),l=lowScene.intersect(origin,{0,-1,0},.0001f,10,lowHit);
        check(f==l&&(!f||glm::distance(fullHit.position,lowHit.position)<1e-5f),"LOD changed planar occlusion / hole visibility");}
    w.draws[1].pathTracingIor=1.5f;auto glass=buildLowLodWorld(w,s,&hotReport,cache);check(hotReport.cacheHits==0&&glass.draws[0].mesh==mesh&&glass.draws[1].mesh==mesh,"Shared optical boundary was simplified / stale LOD reused");
    w.draws.pop_back();w.draws[0].parameters.factors.w=.5f;check(buildLowLodWorld(w,s).draws[0].mesh==mesh,"Alpha cutout was simplified");
    s.enabled=false;check(buildLowLodWorld(w,s).draws[0].mesh==mesh,"Disabled LOD changed geometry");
}

}
