#include "renderer/rhi/ClusterGeometry.h"
#include "renderer/rhi/SceneSnapshot.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <queue>
#include <numeric>
#include <stdexcept>
namespace render {
namespace {
using Edge=std::pair<uint32_t,uint32_t>;
Edge edge(uint32_t a,uint32_t b){return std::minmax(a,b);}
using Face=std::array<uint32_t,3>;
using VertexBounds=std::map<uint32_t,glm::vec3>; // position, UV, unit-normal displacement radii.
struct Work {std::vector<Face> faces;uint32_t node=0;float error=0,uvError=0,normalError=0;VertexBounds bounds;};
struct Quadric {
    glm::dmat4 value{0};
    double cost(glm::vec3 p) const {auto v=glm::dvec4(p,1);return std::max(0.,glm::dot(v,value*v));}
};
std::vector<Face> simplify(const MeshPayload& input,std::vector<Face> faces,const std::set<uint32_t>& inputSeams,
    const ClusterBuildSettings& settings,float childError,float childUv,float childNormal,float& outError,float& outUv,float& outNormal,
    const VertexBounds& inputBounds,VertexBounds& outputBounds,ClusterHierarchy& stats){
    MeshPayload source;std::map<uint32_t,uint32_t> ids;std::vector<uint32_t> original;std::set<uint32_t> seams;
    for(auto& f:faces)for(auto& v:f){auto found=ids.emplace(v,uint32_t(ids.size()));if(found.second){source.vertices.push_back(input.vertices[v]);original.push_back(v);if(inputSeams.count(v))seams.insert(found.first->second);}v=found.first->second;}
    // Topological endpoint contraction: no new positions on shared boundaries.
    const size_t n=source.vertices.size();std::vector<std::set<uint32_t>> incident(n);
    std::vector<Quadric> quadrics(n);std::vector<float> radii(n,childError),uvRadii(n,childUv),normalRadii(n,childNormal);
    if(settings.spatialPartition&&settings.localizedErrors)for(size_t i=0;i<n;++i){auto found=inputBounds.find(original[i]);auto bound=found==inputBounds.end()?glm::vec3(0):found->second;
        radii[i]=bound.x;uvRadii[i]=bound.y;normalRadii[i]=bound.z;}
    std::vector<uint32_t> parent(n),version(n);std::iota(parent.begin(),parent.end(),0);
    std::map<Edge,uint32_t> edgeCounts;
    for(uint32_t t=0;t<faces.size();++t){const auto f=faces[t];auto a=source.vertices[f[0]].position,b=source.vertices[f[1]].position,c=source.vertices[f[2]].position;
        auto normal=glm::cross(b-a,c-a);double area=glm::length(normal);if(area<=1e-12)continue;
        auto plane=glm::dvec4(glm::dvec3(normal)/area,-glm::dot(glm::dvec3(normal)/area,glm::dvec3(a)));
        for(auto v:f){incident[v].insert(t);quadrics[v].value+=glm::outerProduct(plane,plane)*area;}
        for(int k=0;k<3;++k)++edgeCounts[edge(f[k],f[(k+1)%3])];
    }
    std::set<uint32_t> locked=seams;for(auto e:edgeCounts)if(e.second!=2){locked.insert(e.first.first);locked.insert(e.first.second);}
    auto root=[&](uint32_t a){while(parent[a]!=a)a=parent[a];return a;};
    auto live=[](Face f){return f[0]!=f[1]&&f[1]!=f[2]&&f[2]!=f[0];};
    auto current=[&](uint32_t t){auto f=faces[t];for(auto& v:f)v=root(v);return f;};
    struct Candidate {double cost;uint32_t keep,drop,aVersion,bVersion;};
    auto compare=[](const Candidate&a,const Candidate&b){return std::tie(a.cost,a.keep,a.drop)>std::tie(b.cost,b.keep,b.drop);};
    std::priority_queue<Candidate,std::vector<Candidate>,decltype(compare)> queue(compare);
    auto enqueue=[&](uint32_t a,uint32_t b){a=root(a);b=root(b);if(a==b||locked.count(a)||locked.count(b))return;
        const auto& A=source.vertices[a];const auto& B=source.vertices[b];
        if(settings.spatialPartition&&(glm::dot(A.normal,A.normal)<1e-24f||glm::dot(B.normal,B.normal)<1e-24f))return;
        if(glm::dot(glm::normalize(A.normal),glm::normalize(B.normal))<.94f)return;
        Quadric q;q.value=quadrics[a].value+quadrics[b].value;
        auto ca=q.cost(A.position),cb=q.cost(B.position);if(cb<ca || (ca==cb&&b<a))std::swap(a,b);
        queue.push({std::min(ca,cb),a,b,version[a],version[b]});
    };
    for(auto e:edgeCounts)if(e.second==2)enqueue(e.first.first,e.first.second);
    size_t active=faces.size(),target=(active+1)/2;outError=childError;outUv=childUv;outNormal=childNormal;
    while(active>target&&!queue.empty()){
        auto c=queue.top();queue.pop();auto a=c.keep,b=c.drop;
        if(root(a)!=a||root(b)!=b||version[a]!=c.aVersion||version[b]!=c.bVersion)continue;
        float radius=std::max(radii[a],radii[b]+glm::distance(source.vertices[a].position,source.vertices[b].position));
        float uv=std::max(uvRadii[a],uvRadii[b]+glm::distance(source.vertices[a].uv,source.vertices[b].uv));
        auto unit=[](glm::vec3 n){float length=glm::length(n);return length>1e-12f?n/length:glm::vec3(0);};
        float normal=std::max(normalRadii[a],normalRadii[b]+glm::distance(unit(source.vertices[a].normal),unit(source.vertices[b].normal)));
        if(radius>settings.maxError){++stats.positionRejected;continue;}
        if(uv>settings.maxUvError){++stats.uvRejected;continue;}
        if(settings.spatialPartition&&normal>settings.maxNormalError){++stats.normalRejected;continue;}
        auto neighbors=[&](uint32_t v){std::set<uint32_t> result;for(auto t:incident[v]){auto f=current(t);if(live(f))for(auto w:f)if(w!=v)result.insert(w);}return result;};
        auto na=neighbors(a),nb=neighbors(b);std::set<uint32_t> common,opposites;
        std::set_intersection(na.begin(),na.end(),nb.begin(),nb.end(),std::inserter(common,common.begin()));
        for(auto t:incident[a]){auto f=current(t);if(live(f)&&std::find(f.begin(),f.end(),b)!=f.end())for(auto v:f)if(v!=a&&v!=b)opposites.insert(v);}
        if(common!=opposites||opposites.size()!=2)continue; // manifold link condition.
        auto affected=incident[a];affected.insert(incident[b].begin(),incident[b].end());bool safe=true;
        for(auto t:affected){auto f=current(t);if(!live(f))continue;auto next=f;for(auto& v:next)if(v==b)v=a;if(!live(next))continue;
            auto normal=[&](Face g){return glm::cross(source.vertices[g[1]].position-source.vertices[g[0]].position,source.vertices[g[2]].position-source.vertices[g[0]].position);};
            auto oldN=normal(f),newN=normal(next);if(glm::length(newN)<=1e-12f||glm::dot(oldN,newN)<=0){safe=false;break;}}
        if(!safe)continue;
        parent[b]=a;quadrics[a].value+=quadrics[b].value;radii[a]=radius;uvRadii[a]=uv;normalRadii[a]=normal;outError=std::max(outError,radius);outUv=std::max(outUv,uv);outNormal=std::max(outNormal,normal);
        incident[a]=std::move(affected);incident[b].clear();++version[a];++version[b];active-=2;
        for(auto v:na)enqueue(a,v);for(auto v:nb)enqueue(a,v);
    }
    std::vector<Face> out;for(uint32_t t=0;t<faces.size();++t){auto f=current(t);if(live(f)){for(auto& v:f){
        if(settings.spatialPartition&&settings.localizedErrors)outputBounds[original[v]]=glm::vec3(radii[v],uvRadii[v],normalRadii[v]);
        v=original[v];}out.push_back(f);}}return out;
}
std::vector<std::vector<Face>> legacyPartition(const std::vector<Face>& faces,uint32_t capacity){
    std::map<Edge,std::vector<uint32_t>> edges;
    for(uint32_t t=0;t<faces.size();++t)for(int k=0;k<3;++k)edges[edge(faces[t][k],faces[t][(k+1)%3])].push_back(t);
    std::vector<std::vector<uint32_t>> adjacency(faces.size());
    for(const auto& e:edges)if(e.second.size()==2){auto a=e.second[0],b=e.second[1];adjacency[a].push_back(b);adjacency[b].push_back(a);}
    std::vector<bool> visited(faces.size());std::vector<std::vector<Face>> clusters;
    for(uint32_t seed=0;seed<faces.size();++seed)if(!visited[seed]){
        clusters.emplace_back();std::deque<uint32_t> frontier{seed};
        while(!frontier.empty()&&clusters.back().size()<capacity){auto t=frontier.front();frontier.pop_front();if(visited[t])continue;
            visited[t]=true;clusters.back().push_back(faces[t]);for(auto v:adjacency[t])if(!visited[v])frontier.push_back(v);
        }
    }
    return clusters;
}
// Geometry adjacency is independent of normal/UV/tangent addresses. It never
// changes those addresses, and the simplifier still locks every attribute seam.
struct PartitionContext {
    const MeshPayload& mesh;const std::vector<uint32_t>& topology;
    std::vector<uint32_t> vertexStamp,positionStamp;uint32_t stamp=0;
    PartitionContext(const MeshPayload& m,const std::vector<uint32_t>& t):mesh(m),topology(t),vertexStamp(m.vertices.size()),positionStamp(m.vertices.size()){}
    Edge geometricEdge(uint32_t a,uint32_t b)const{return edge(topology[a],topology[b]);}
};
uint32_t mortonBits(uint32_t x){x&=1023u;x=(x|(x<<16))&0x030000ffu;x=(x|(x<<8))&0x0300f00fu;x=(x|(x<<4))&0x030c30c3u;return (x|(x<<2))&0x09249249u;}
std::vector<std::vector<Face>> spatialPartition(const std::vector<Face>& faces,uint32_t capacity,PartitionContext& context){
    if(faces.empty())return {};
    struct EdgeFace {Edge key;uint32_t face;};std::vector<EdgeFace> edges;edges.reserve(faces.size()*3);
    std::vector<glm::vec3> centers(faces.size());glm::vec3 low(INFINITY),high(-INFINITY);
    for(uint32_t t=0;t<faces.size();++t){const auto& f=faces[t];centers[t]=(context.mesh.vertices[f[0]].position+context.mesh.vertices[f[1]].position+context.mesh.vertices[f[2]].position)/3.f;
        low=glm::min(low,centers[t]);high=glm::max(high,centers[t]);for(int k=0;k<3;++k)edges.push_back({context.geometricEdge(f[k],f[(k+1)%3]),t});}
    std::sort(edges.begin(),edges.end(),[](const auto& a,const auto& b){return std::tie(a.key,a.face)<std::tie(b.key,b.face);});
    std::vector<std::vector<uint32_t>> adjacent(faces.size());
    for(size_t first=0;first<edges.size();){size_t last=first+1;while(last<edges.size()&&edges[last].key==edges[first].key)++last;
        // Never invent connectivity at a nonmanifold geometric edge.
        if(last-first==2&&edges[first].face!=edges[first+1].face){auto a=edges[first].face,b=edges[first+1].face;adjacent[a].push_back(b);adjacent[b].push_back(a);}first=last;}
    std::vector<std::pair<uint32_t,uint32_t>> order;order.reserve(faces.size());
    auto extent=glm::max(high-low,glm::vec3(1e-20f));
    for(uint32_t t=0;t<faces.size();++t){auto q=glm::uvec3(glm::clamp((centers[t]-low)/extent,glm::vec3(0),glm::vec3(1))*1023.f);
        order.emplace_back(mortonBits(q.x)|(mortonBits(q.y)<<1)|(mortonBits(q.z)<<2),t);}
    std::sort(order.begin(),order.end());std::vector<uint8_t> visited(faces.size()),queued(faces.size());
    size_t cursor=0;std::vector<std::vector<Face>> out;
    while(cursor<order.size()){
        while(cursor<order.size()&&visited[order[cursor].second])++cursor;if(cursor==order.size())break;
        const auto seed=order[cursor].second;out.emplace_back();auto& cluster=out.back();cluster.reserve(capacity);++context.stamp;
        glm::vec3 boxLow(INFINITY),boxHigh(-INFINITY);float seedScale=0;
        for(int k=0;k<3;++k){auto d=context.mesh.vertices[faces[seed][k]].position-context.mesh.vertices[faces[seed][(k+1)%3]].position;seedScale=std::max(seedScale,glm::dot(d,d));}
        std::vector<uint32_t> frontier{seed};queued[seed]=1;
        while(cluster.size()<capacity){
            uint32_t best=UINT32_MAX;size_t slot=0;int bestReuse=-1;float bestCost=INFINITY;
            for(size_t i=0;i<frontier.size();++i){auto t=frontier[i];if(visited[t])continue;int reuse=0;auto a=boxLow,b=boxHigh;
                for(auto v:faces[t]){reuse+=2*(context.vertexStamp[v]==context.stamp)+(context.positionStamp[context.topology[v]]==context.stamp);a=glm::min(a,context.mesh.vertices[v].position);b=glm::max(b,context.mesh.vertices[v].position);}
                auto d=b-a;float cost=glm::dot(d,d);if(reuse>bestReuse||(reuse==bestReuse&&(cost<bestCost||(cost==bestCost&&t<best)))){best=t;slot=i;bestReuse=reuse;bestCost=cost;}}
            if(best==UINT32_MAX){
                // Fill a stranded frontier from a bounded Morton neighborhood.
                // The radius prevents a meshlet spanning widely separated parts;
                // merely sharing a meshlet does not weld disconnected vertices.
                while(cursor<order.size()&&visited[order[cursor].second])++cursor;
                auto center=(boxLow+boxHigh)*.5f,span=boxHigh-boxLow;float limit=std::max(glm::dot(span,span)*2.25f,seedScale*64.f);
                size_t inspected=0;float distance=INFINITY;
                for(size_t i=cursor;i<order.size()&&i<cursor+256&&inspected<32;++i){auto t=order[i].second;if(visited[t])continue;++inspected;auto d=centers[t]-center;float candidate=glm::dot(d,d);
                    if(candidate<=limit&&(candidate<distance||(candidate==distance&&t<best))){best=t;distance=candidate;}}
                if(best==UINT32_MAX)break;
            }else{frontier[slot]=frontier.back();frontier.pop_back();}
            visited[best]=1;cluster.push_back(faces[best]);for(auto v:faces[best]){context.vertexStamp[v]=context.stamp;context.positionStamp[context.topology[v]]=context.stamp;boxLow=glm::min(boxLow,context.mesh.vertices[v].position);boxHigh=glm::max(boxHigh,context.mesh.vertices[v].position);}
            for(auto t:adjacent[best])if(!visited[t]&&!queued[t]){queued[t]=1;frontier.push_back(t);}
        }
        // Pending faces must be eligible in the next meshlet.
        for(auto t:frontier)queued[t]=0;
    }
    return out;
}
}
std::shared_ptr<const ClusterHierarchy> buildClusterHierarchy(const MeshPayload& mesh,const ClusterBuildSettings& settings){
    static_assert(sizeof(ClusterNode)==48&&sizeof(GeometryCluster)==48,"Cluster storage ABI");
    if(!settings.trianglesPerCluster||settings.trianglesPerCluster>128||!settings.maxLevels||settings.maxLevels>24||
       !std::isfinite(settings.maxError)||settings.maxError<=0||!std::isfinite(settings.maxUvError)||settings.maxUvError<0||!std::isfinite(settings.maxNormalError)||settings.maxNormalError<0||settings.maxNormalError>2||
       !std::isfinite(settings.minParentReduction)||settings.minParentReduction<0||settings.minParentReduction>=1||
       settings.parentGroupSize<2||settings.parentGroupSize>16||
       mesh.indices.size()%3||mesh.indices.size()>UINT32_MAX||mesh.vertices.size()>UINT32_MAX)throw std::invalid_argument("Invalid cluster build input");
    auto started=std::chrono::steady_clock::now();auto result=std::make_shared<ClusterHierarchy>();
    // Weld only bit-identical attributes; retain original vertex addresses.
    std::map<std::array<uint32_t,12>,uint32_t> canonical;std::vector<uint32_t> remap(mesh.vertices.size());
    std::map<std::array<uint32_t,3>,std::set<uint32_t>> positions;std::vector<uint32_t> topology(mesh.vertices.size());
    if(!mesh.pathTracingTangents.empty()&&mesh.pathTracingTangents.size()!=mesh.vertices.size())throw std::invalid_argument("Cluster tangent count mismatch");
    for(uint32_t i=0;i<mesh.vertices.size();++i){std::array<uint32_t,12> key{};const auto& v=mesh.vertices[i];
        std::array<float,12> values{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y,0,0,0,0};
        if(!mesh.pathTracingTangents.empty())for(int k=0;k<4;++k)values[8+k]=mesh.pathTracingTangents[i][k];
        for(int k=0;k<12;++k){if(!std::isfinite(values[k]))throw std::invalid_argument("Nonfinite cluster vertex");if(values[k]!=0)std::memcpy(&key[k],&values[k],4);}
        remap[i]=canonical.emplace(key,i).first->second;auto& at=positions[{key[0],key[1],key[2]}];at.insert(remap[i]);topology[i]=*at.begin();
    }
    std::set<uint32_t> seams;for(const auto& p:positions)if(p.second.size()>1)seams.insert(p.second.begin(),p.second.end());
    result->positionVertices=uint32_t(positions.size());result->attributeVertices=uint32_t(canonical.size());result->seamVertices=uint32_t(seams.size());result->spatialPartition=settings.spatialPartition;
    PartitionContext partitionContext(mesh,topology);
    auto partition=[&](const std::vector<Face>& f){return settings.spatialPartition?spatialPartition(f,settings.trianglesPerCluster,partitionContext):legacyPartition(f,settings.trianglesPerCluster);};
    std::vector<Face> faces;
    for(size_t t=0;t<mesh.indices.size();t+=3){Face f;for(int k=0;k<3;++k){if(mesh.indices[t+k]>=remap.size())throw std::invalid_argument("Cluster index out of bounds");f[k]=remap[mesh.indices[t+k]];}
        auto normal=glm::cross(mesh.vertices[f[1]].position-mesh.vertices[f[0]].position,mesh.vertices[f[2]].position-mesh.vertices[f[0]].position);
        if(glm::dot(normal,normal)>1e-24f)faces.push_back(f);
    }
    result->sourceIndices=uint32_t(faces.size()*3);
    if(faces.empty())return result;
    auto add=[&](const std::vector<Face>& geometry,float error,float uvError,uint32_t left,uint32_t right,uint32_t level){
        ClusterNode node;node.lowError=glm::vec4(INFINITY,INFINITY,INFINITY,error);node.high=glm::vec4(-INFINITY);
        for(const auto& f:geometry)for(auto v:f){node.lowError=glm::vec4(glm::min(glm::vec3(node.lowError),mesh.vertices[v].position),error);node.high=glm::vec4(glm::max(glm::vec3(node.high),mesh.vertices[v].position),0);}
        if(left!=UINT32_MAX){node.lowError=glm::vec4(glm::min(glm::vec3(result->nodes[left].lowError),glm::vec3(result->nodes[right].lowError)),error);node.high=glm::vec4(glm::max(glm::vec3(result->nodes[left].high),glm::vec3(result->nodes[right].high)),0);}
        node.high.w=uvError;uint32_t id=uint32_t(result->nodes.size());node.links={UINT32_MAX,left,right,level};result->nodes.push_back(node);
        if(left!=UINT32_MAX){result->nodes[left].links.x=id;result->nodes[right].links.x=id;}
        // Leaf groups are already bounded meshlets. Repartitioning them could
        // split a spatially filled leaf again and duplicate sorting work.
        const auto meshlets=settings.spatialPartition&&left==UINT32_MAX?std::vector<std::vector<Face>>{geometry}:partition(geometry);
        for(const auto& cluster:meshlets){
            GeometryCluster c;c.low=glm::vec4(INFINITY);c.high=glm::vec4(-INFINITY);c.range={uint32_t(result->indices.size()),uint32_t(cluster.size()*3),id,0};
            for(auto f:cluster)for(auto v:f){c.low=glm::vec4(glm::min(glm::vec3(c.low),mesh.vertices[v].position),0);c.high=glm::vec4(glm::max(glm::vec3(c.high),mesh.vertices[v].position),0);result->indices.push_back(v);}
            result->clusters.push_back(c);
        }
        return id;
    };
    auto leaves=partition(faces);
    if(faces.size()*12+leaves.size()*96>settings.byteBudget)throw std::runtime_error("Cluster leaf representation exceeds byte budget");
    std::vector<Work> layer;for(auto& group:leaves)layer.push_back({group,add(group,0,0,UINT32_MAX,UINT32_MAX,0),0,0});
    result->leafClusters=uint32_t(result->clusters.size());
    for(uint32_t level=1;layer.size()>1&&level<settings.maxLevels;++level){
        std::map<Edge,std::vector<uint32_t>> shared;
        for(uint32_t i=0;i<layer.size();++i){std::set<Edge> edges;for(auto f:layer[i].faces)for(int k=0;k<3;++k)edges.insert(settings.spatialPartition?partitionContext.geometricEdge(f[k],f[(k+1)%3]):edge(f[k],f[(k+1)%3]));for(auto e:edges)shared[e].push_back(i);}
        std::vector<std::map<uint32_t,uint32_t>> neighbors(layer.size());
        for(const auto& e:shared)if(e.second.size()==2){auto a=e.second[0],b=e.second[1];++neighbors[a][b];++neighbors[b][a];}
        std::vector<bool> used(layer.size());std::vector<Work> next;
        for(uint32_t a=0;a<layer.size();++a)if(!used[a]){
            std::vector<uint32_t> members{a};std::map<uint32_t,uint32_t> frontier=neighbors[a];
            const auto capacity=settings.spatialPartition?settings.parentGroupSize:2u;
            while(members.size()<capacity){uint32_t best=UINT32_MAX,weight=0;
                for(auto p:frontier)if(!used[p.first]&&std::find(members.begin(),members.end(),p.first)==members.end()&&
                    (p.second>weight||(p.second==weight&&p.first<best))){best=p.first;weight=p.second;}
                if(best==UINT32_MAX)break;
                members.push_back(best);for(auto p:neighbors[best])frontier[p.first]+=p.second;
            }
            // Disconnected components remain separate roots; grouping never welds attributes.
            if(members.size()==1){next.push_back(std::move(layer[a]));used[a]=true;continue;}
            std::vector<Face> merged;VertexBounds inherited,outputBounds;
            float error=0,uvError=0,normalError=0;
            glm::vec3 coverageLow(INFINITY),coverageHigh(-INFINITY);
            for(auto i:members){const auto& child=layer[i];merged.insert(merged.end(),child.faces.begin(),child.faces.end());
                error=std::max(error,child.error);uvError=std::max(uvError,child.uvError);normalError=std::max(normalError,child.normalError);
                coverageLow=glm::min(coverageLow,glm::vec3(result->nodes[child.node].lowError));coverageHigh=glm::max(coverageHigh,glm::vec3(result->nodes[child.node].high));
                for(const auto& b:child.bounds)inherited[b.first]=glm::max(inherited[b.first],b.second);
            }
            size_t required=result->indices.size()*4+result->clusters.size()*48+result->nodes.size()*48+merged.size()*(12+48)+(members.size()-1)*48;
            if(required>settings.byteBudget||result->indices.size()+merged.size()*3>UINT32_MAX){result->budgetLimited=true;next.push_back(std::move(layer[a]));used[a]=true;continue;}
            ++result->parentAttempts;
            const size_t mergedSize=merged.size();auto low=simplify(mesh,std::move(merged),seams,settings,error,uvError,normalError,error,uvError,normalError,inherited,outputBounds,*result);
            // A parent must save enough triangles to justify its resident copy.
            // Locked seams/normals remain protected instead of forcing a deeper
            // hierarchy with many near-identical index representations.
            if(settings.spatialPartition&&(low.size()==mergedSize||double(low.size())>double(mergedSize)*(1-settings.minParentReduction))){++result->reductionRejected;next.push_back(std::move(layer[a]));used[a]=true;continue;}
            result->normalErrorBound=std::max(result->normalErrorBound,normalError);
            // Fan-in is encoded with binary routing nodes without index copies.
            // Every router has EXACTLY the drawable parent's error and coverage,
            // so its projected error is identical in every camera/transform:
            // either the parent is selected, or all original child cuts remain.
            auto route=[&](auto&& self,size_t first,size_t last)->uint32_t{
                if(last-first==1)return layer[members[first]].node;
                const auto middle=(first+last)/2;auto left=self(self,first,middle),right=self(self,middle,last);
                ClusterNode node;node.lowError=glm::vec4(coverageLow,error);node.high=glm::vec4(coverageHigh,uvError);
                node.links={UINT32_MAX,left,right,level};auto id=uint32_t(result->nodes.size());result->nodes.push_back(node);
                result->nodes[left].links.x=id;result->nodes[right].links.x=id;++result->routingNodes;return id;
            };
            const auto middle=members.size()/2;auto left=route(route,0,middle),right=route(route,middle,members.size());
            uint32_t node=add(low,error,uvError,left,right,level);
            // Preserve whole source coverage even when routing nodes are absent.
            result->nodes[node].lowError=glm::vec4(coverageLow,error);result->nodes[node].high=glm::vec4(coverageHigh,uvError);
            next.push_back({std::move(low),node,error,uvError,normalError,std::move(outputBounds)});
            for(auto i:members)used[i]=true;++result->parentAccepted;
        }
        if(next.size()==layer.size()){layer=std::move(next);break;}layer=std::move(next);
    }
    for(const auto& group:layer)result->roots.push_back(group.node);
    result->buildSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();return result;
}
std::vector<uint32_t> selectClusterNodes(const ClusterHierarchy& h,const glm::mat4& vm,float focal,float scale,float nearPlane,float budget,bool ortho,bool allow){
    if(!std::isfinite(budget)||budget<0||!std::isfinite(focal)||focal<=0||!std::isfinite(scale)||scale<=0||!std::isfinite(nearPlane)||nearPlane<=0)throw std::invalid_argument("Invalid cluster cut settings");
    auto error=[&](const ClusterNode& node){if(node.lowError.w==0)return 0.f;auto center=(glm::vec3(node.lowError)+glm::vec3(node.high))*.5f,extent=(glm::vec3(node.high)-glm::vec3(node.lowError))*.5f;
        auto depth=-(vm*glm::vec4(center,1)).z-glm::dot(glm::abs(glm::vec3(vm[0][2],vm[1][2],vm[2][2])),extent);
        return node.lowError.w*scale*focal/(ortho?1.f:std::max(depth,nearPlane));};
    std::vector<uint32_t> result;
    for(uint32_t i=0;i<h.nodes.size();++i){const auto& n=h.nodes[i];bool leaf=n.links.y==UINT32_MAX;
        if(!allow){if(leaf)result.push_back(i);continue;}
        if(!leaf&&error(n)>budget)continue;
        if(n.links.x==UINT32_MAX||error(h.nodes[n.links.x])>budget)result.push_back(i);
    }
    return result;
}
GpuClusterGeometry::GpuClusterGeometry(std::shared_ptr<rhi::GraphicsDevice> d,std::shared_ptr<const ClusterHierarchy> h):hierarchy(std::move(h)),resources_(d){
    if(hierarchy->indices.empty())throw std::invalid_argument("Empty GPU cluster hierarchy");
    using namespace rhi;nodes=resources_.buffer({hierarchy->nodes.size()*sizeof(ClusterNode),BufferUsage::Storage,"Cluster hierarchy nodes"},hierarchy->nodes.data());
    clusters=resources_.buffer({hierarchy->clusters.size()*sizeof(GeometryCluster),BufferUsage::Storage,"Cluster geometry ranges"},hierarchy->clusters.data());
    indices=resources_.buffer({hierarchy->indices.size()*4,BufferUsage::Storage,"Cluster hierarchy indices"},hierarchy->indices.data());
}
}
