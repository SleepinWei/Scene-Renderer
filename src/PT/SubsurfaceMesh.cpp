#include "PT/SubsurfaceMesh.h"
#include <array>
#include <cmath>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
namespace pt {
MeshClosure closeSubsurfaceMesh(render::MeshPayload &mesh,float maximumHoleRadius){
    if(!std::isfinite(maximumHoleRadius)||maximumHoleRadius<=0)throw std::invalid_argument("PT: invalid scan repair radius");
    MeshClosure report;std::map<std::array<int64_t,3>,uint32_t> positions;std::vector<render::MeshVertex> vertices;std::vector<uint32_t> remap;
    for(auto v:mesh.vertices){auto p=v.position;if(!std::isfinite(glm::length(p)))throw std::invalid_argument("PT: non-finite scan vertex");std::array<int64_t,3> key{int64_t(std::llround(p.x*1e6)),int64_t(std::llround(p.y*1e6)),int64_t(std::llround(p.z*1e6))};auto [it,newVertex]=positions.emplace(key,uint32_t(vertices.size()));if(newVertex)vertices.push_back(v);remap.push_back(it->second);}
    report.weldedVertices=uint32_t(mesh.vertices.size()-vertices.size());std::vector<uint32_t> faces;
    if(mesh.indices.size()%3)throw std::invalid_argument("PT: malformed scan triangle indices");
    for(size_t i=0;i<mesh.indices.size();i+=3){uint32_t a=remap.at(mesh.indices[i]),b=remap.at(mesh.indices[i+1]),c=remap.at(mesh.indices[i+2]);auto n=glm::cross(vertices[b].position-vertices[a].position,vertices[c].position-vertices[a].position);if(a==b||b==c||a==c||glm::dot(n,n)<=1e-24f){++report.removedFaces;continue;}faces.insert(faces.end(),{a,b,c});}
    struct Edge {uint32_t a=0,b=0,count=0;};std::unordered_map<uint64_t,Edge> edges;edges.reserve(faces.size());
    auto add=[&](uint32_t a,uint32_t b){uint64_t key=(uint64_t(std::min(a,b))<<32)|std::max(a,b);auto &e=edges[key];if(e.count==0){e.a=a;e.b=b;}else if(e.count!=1||e.a!=b||e.b!=a)throw std::invalid_argument("PT: subsurface scan has non-manifold/inconsistently wound edges");++e.count;};
    for(size_t i=0;i<faces.size();i+=3){add(faces[i],faces[i+1]);add(faces[i+1],faces[i+2]);add(faces[i+2],faces[i]);}
    std::unordered_map<uint32_t,uint32_t> next;std::unordered_map<uint32_t,uint32_t> incoming;
    for(const auto &[key,e]:edges)if(e.count==1){++report.boundaryEdges;if(!next.emplace(e.a,e.b).second||!incoming.emplace(e.b,e.a).second)throw std::invalid_argument("PT: scan boundary branches; cannot repair subsurface mesh");}
    for(const auto &[a,b]:next)if(!incoming.count(a))throw std::invalid_argument("PT: open scan boundary chain");
    while(!next.empty()){
        const uint32_t start=next.begin()->first;uint32_t current=start;std::vector<uint32_t> loop;
        do{auto it=next.find(current);if(it==next.end()||loop.size()>4096)throw std::invalid_argument("PT: scan hole is not a simple loop");loop.push_back(current);current=it->second;next.erase(it);}while(current!=start);
        if(loop.size()<3)throw std::invalid_argument("PT: degenerate scan hole");glm::vec3 center(0),normal(0);for(auto id:loop){center+=vertices[id].position;normal+=vertices[id].normal;}center/=float(loop.size());
        float radius=0;for(auto id:loop)radius=std::max(radius,glm::length(vertices[id].position-center));if(radius>maximumHoleRadius)throw std::invalid_argument("PT: scan hole exceeds subsurface repair radius: "+std::to_string(radius));
        // Each new boundary edge is reversed, preserving the original winding.
        uint32_t id=uint32_t(vertices.size());if(glm::dot(normal,normal)>1e-20f)normal=glm::normalize(normal);else normal={0,1,0};
        bool collapsed=false;for(size_t i=0;i<loop.size();++i){auto n=glm::cross(vertices[loop[(i+1)%loop.size()]].position-center,vertices[loop[i]].position-center);collapsed|=glm::dot(n,n)<=1e-24f;}
        // Collinear micro-cracks need a tiny cap displacement to form intersectable faces.
        if(collapsed)center+=normal*std::max(1e-5f,radius*.001f);vertices.push_back({center,normal,{0,0}});
        for(size_t i=0;i<loop.size();++i){auto a=loop[i],b=loop[(i+1)%loop.size()];auto n=glm::cross(vertices[b].position-center,vertices[a].position-center);if(glm::dot(n,n)<=1e-24f)throw std::invalid_argument("PT: degenerate scan hole triangulation");faces.insert(faces.end(),{id,b,a});}++report.filledHoles;
    }
    // Verify the repaired mesh rather than trusting the fan construction.
    edges.clear();for(size_t i=0;i<faces.size();i+=3){add(faces[i],faces[i+1]);add(faces[i+1],faces[i+2]);add(faces[i+2],faces[i]);}for(auto &[key,e]:edges)if(e.count!=2)throw std::invalid_argument("PT: scan repair left an open boundary");
    double signedVolume=0;for(size_t i=0;i<faces.size();i+=3)signedVolume+=glm::dot(glm::dvec3(vertices[faces[i]].position),glm::cross(glm::dvec3(vertices[faces[i+1]].position),glm::dvec3(vertices[faces[i+2]].position)))/6;std::cout<<"Closed scan signed volume "<<signedVolume<<"\n";if(signedVolume<0){for(size_t i=0;i<faces.size();i+=3)std::swap(faces[i+1],faces[i+2]);for(auto &v:vertices)v.normal=-v.normal;}
    mesh.vertices=std::move(vertices);mesh.indices=std::move(faces);report.boundaryEdges=0;return report;
}
}
