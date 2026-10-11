#include "renderer/rhi/ClusterGeometry.h"
#include "renderer/rhi/GpuClusterCuller.h"
#include "renderer/rhi/GpuHiZCuller.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "rhi/ShaderAssets.h"
#include <glm/gtc/matrix_transform.hpp>
#include <json/json.hpp>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <stb/stb_image_write.h>
namespace render {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(std::string("Cluster validation: ")+message);}
MeshPayload grid(uint32_t n=48){MeshPayload m;m.id=999;m.revision=1;
    for(uint32_t z=0;z<=n;++z)for(uint32_t x=0;x<=n;++x){float X=float(x)/n*8-4,Z=float(z)/n*8-4;
        float height=.12f*std::sin(X)*std::cos(Z);glm::vec3 normal=glm::normalize(glm::vec3(-.12f*std::cos(X)*std::cos(Z),1,.12f*std::sin(X)*std::sin(Z)));
        m.vertices.push_back({{X,height,Z},normal,{float(x)/n*.1f,float(z)/n*.1f}});}
    for(uint32_t z=0;z<n;++z)for(uint32_t x=0;x<n;++x){auto a=z*(n+1)+x,b=a+1,c=a+n+1,d=c+1;m.indices.insert(m.indices.end(),{a,c,b,b,c,d});}
    return m;
}
std::vector<uint32_t> cutIndices(const ClusterHierarchy& h,const std::vector<uint32_t>& cut){std::set<uint32_t> selected(cut.begin(),cut.end());std::vector<uint32_t> out;
    for(const auto& c:h.clusters)if(selected.count(c.range.z))out.insert(out.end(),h.indices.begin()+c.range.x,h.indices.begin()+c.range.x+c.range.y);return out;}
using Edge=std::pair<uint32_t,uint32_t>;
std::set<Edge> boundaries(const std::vector<uint32_t>& indices){std::map<Edge,uint32_t> edges;
    for(size_t i=0;i<indices.size();i+=3)for(int k=0;k<3;++k)++edges[std::minmax(indices[i+k],indices[i+(k+1)%3])];
    std::set<Edge> result;for(auto p:edges){require(p.second<=2,"nonmanifold edge in mixed cut");if(p.second==1)result.insert(p.first);}return result;}
void validateCut(const ClusterHierarchy& h,const std::vector<uint32_t>& cut){std::set<uint32_t> chosen(cut.begin(),cut.end());
    for(uint32_t i=0;i<h.nodes.size();++i)if(h.nodes[i].links.y==UINT32_MAX){uint32_t visits=0;
        for(auto j=i;j!=UINT32_MAX;j=h.nodes[j].links.x)visits+=chosen.count(j);require(visits==1,"leaf coverage duplicated or omitted");}
}
void png(const std::filesystem::path& path,uint32_t w,uint32_t h,const std::vector<uint8_t>& rgba){require(stbi_write_png(path.string().c_str(),w,h,4,rgba.data(),w*4)!=0,"PNG write");}
}
void validateClusterHierarchy(){
    auto source=grid();auto original=source.indices;auto h=buildClusterHierarchy(source);require(source.indices==original,"source modified");
    auto repeat=buildClusterHierarchy(source);require(h->indices==repeat->indices&&h->nodes.size()==repeat->nodes.size(),"nondeterministic hierarchy");
    require(h->sourceIndices==source.indices.size()&&h->leafClusters>1&&h->nodes.size()>h->leafClusters,"hierarchy not built");
    require(h->normalErrorBound<=ClusterBuildSettings{}.maxNormalError,"accumulated normal error exceeded construction ceiling");
    for(const auto& c:h->clusters)require(c.range.y<=128*3&&c.range.y>0,"meshlet triangle capacity");
    std::vector<uint64_t> groupIndices(h->nodes.size());for(const auto& c:h->clusters)groupIndices[c.range.z]+=c.range.y;
    for(size_t i=0;i<h->nodes.size();++i)if(h->nodes[i].links.y!=UINT32_MAX){const auto& n=h->nodes[i];
        require(double(groupIndices[i])<=double(groupIndices[n.links.y]+groupIndices[n.links.z])*(1-ClusterBuildSettings{}.minParentReduction),"parent copy has insufficient triangle reduction");}
    for(const auto& n:h->nodes)if(n.links.x!=UINT32_MAX){const auto& parent=h->nodes[n.links.x];require(parent.lowError.w>=n.lowError.w,"error not monotone");
        for(int k=0;k<3;++k)require(parent.lowError[k]<=n.lowError[k]&&parent.high[k]>=n.high[k],"coverage bounds not nested");}
    auto vm=glm::lookAt(glm::vec3(2,6,10),glm::vec3(0),glm::vec3(0,1,0));size_t previous=source.indices.size(),smallest=previous;bool mixed=false;
    for(float budget:{0.f,.25f,1.f,4.f,64.f}){auto cut=selectClusterNodes(*h,vm,500,1,.1f,budget);validateCut(*h,cut);auto indices=cutIndices(*h,cut);
        require(indices.size()<=previous,"coarser budget increased triangles");previous=indices.size();smallest=std::min(smallest,indices.size());
        require(boundaries(indices)==boundaries(original),"mixed LOD cut opened cracks or changed borders");
        std::set<uint32_t> levels;for(auto n:cut)levels.insert(h->nodes[n].links.w);mixed|=levels.size()>1;
    }
    require(smallest<source.indices.size()*.8&&mixed,"no meaningful adaptive reduction");
    auto allLeaves=selectClusterNodes(*h,vm,500,1,.1f,999,false,false);validateCut(*h,allLeaves);require(cutIndices(*h,allLeaves).size()==source.indices.size(),"protected geometry reduced");
    ClusterBuildSettings limited;limited.byteBudget=source.indices.size()*4+48*h->leafClusters*2+1024;auto lowBudget=buildClusterHierarchy(source,limited);
    validateCut(*lowBudget,selectClusterNodes(*lowBudget,vm,500,1,.1f,64));require(lowBudget->budgetLimited,"construction budget ignored");
    auto disjoint=source;uint32_t base=uint32_t(disjoint.vertices.size());for(auto v:source.vertices){v.position.x+=20;disjoint.vertices.push_back(v);}for(auto i:source.indices)disjoint.indices.push_back(i+base);
    auto components=buildClusterHierarchy(disjoint);require(components->roots.size()>=2,"disconnected surfaces grouped together");
    // Attribute-split triangles still form one geometric surface. Keep their
    // original UV/normal/tangent addresses, but do not make one meshlet per face.
    MeshPayload split;for(size_t t=0;t<source.indices.size();t+=3)for(int k=0;k<3;++k){auto v=source.vertices[source.indices[t+k]];v.uv.x+=float(t)*.001f;
        split.indices.push_back(uint32_t(split.vertices.size()));split.vertices.push_back(v);split.pathTracingTangents.push_back({1,0,0,t%2?1.f:-1.f});}
    ClusterBuildSettings legacy;legacy.spatialPartition=false;auto oldSplit=buildClusterHierarchy(split,legacy),newSplit=buildClusterHierarchy(split);
    require(newSplit->leafClusters*4<oldSplit->leafClusters,"geometric adjacency did not improve seam-split packing");
    auto sortedFaces=[](const std::vector<uint32_t>& indices){std::vector<std::array<uint32_t,3>> faces;
        for(size_t i=0;i<indices.size();i+=3){std::array<uint32_t,3> f{indices[i],indices[i+1],indices[i+2]};auto start=std::min_element(f.begin(),f.end())-f.begin();faces.push_back({f[start],f[(start+1)%3],f[(start+2)%3]});}
        std::sort(faces.begin(),faces.end());return faces;};
    for(float budget:{0.f,4.f,64.f}){auto cut=selectClusterNodes(*newSplit,vm,500,1,.1f,budget);validateCut(*newSplit,cut);
        require(sortedFaces(cutIndices(*newSplit,cut))==sortedFaces(split.indices),"attribute-split packing changed vertex addresses, winding or triangle coverage");}
    require(newSplit->positionVertices<newSplit->attributeVertices&&newSplit->seamVertices>newSplit->attributeVertices*.99,"attribute seam detection lost split corners");
    // A two-chart seam must keep its exact chart boundaries at mixed LOD.
    MeshPayload charts;std::map<std::pair<uint32_t,bool>,uint32_t> chartVertices;
    for(size_t t=0;t<source.indices.size();t+=3){bool right=source.vertices[source.indices[t]].position.x>=0;
        for(int k=0;k<3;++k){auto original=source.indices[t+k];auto at=chartVertices.emplace(std::make_pair(original,right),uint32_t(charts.vertices.size()));
            if(at.second){auto v=source.vertices[original];v.uv.x+=right?.5f:0;charts.vertices.push_back(v);charts.pathTracingTangents.push_back({1,0,0,right?-1.f:1.f});}charts.indices.push_back(at.first->second);}}
    auto chartHierarchy=buildClusterHierarchy(charts);for(float budget:{0.f,1.f,4.f,64.f}){auto cut=selectClusterNodes(*chartHierarchy,vm,500,1,.1f,budget);validateCut(*chartHierarchy,cut);
        require(boundaries(cutIndices(*chartHierarchy,cut))==boundaries(charts.indices),"mixed LOD changed UV/tangent chart boundaries");}
    auto varyingNormals=grid(12);for(auto& v:varyingNormals.vertices)v.normal=glm::normalize(glm::vec3(v.position.x*.1f,1,v.position.z*.1f));
    ClusterBuildSettings exactNormals;exactNormals.maxNormalError=0;auto normalLocked=buildClusterHierarchy(varyingNormals,exactNormals);
    require(cutIndices(*normalLocked,selectClusterNodes(*normalLocked,vm,500,1,.1f,999)).size()==varyingNormals.indices.size(),"zero normal error ceiling allowed distinct endpoint normals to collapse");
    std::cout<<"Attribute seam packing: "<<oldSplit->leafClusters<<" -> "<<newSplit->leafClusters<<" leaf meshlets; original corners and chart boundaries preserved\n";
    std::cout<<"Cluster hierarchy: "<<h->leafClusters<<" leaf meshlets, "<<h->nodes.size()<<" groups, "<<source.indices.size()/3<<" -> "<<smallest/3<<" triangles; mixed cuts preserve exact boundary edges\n";
}
void validateClusterRendering(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    size_t checkedMipTexels=0;
    for(const auto size:std::array<glm::uvec2,8>{glm::uvec2(1,1),{2,1},{3,7},{15,17},{17,15},{65,33},{257,129},{960,540}}){
        Resources input(device);std::vector<float> values(size_t(size.x)*size.y);
        for(size_t i=0;i<values.size();++i)values[i]=i%19==0?1.f:float((i*1664525u+1013904223u)%65536)/65536.f;
        auto texture=input.texture({size.x,size.y,rhi::Format::R32Float,rhi::TextureUsage::Sampled|rhi::TextureUsage::CopyDestination,"HiZ random oracle source"});device->writeTextureFloat(texture,values.data(),values.size()*4);auto sourceView=input.view(texture);
        for(uint32_t baseMip:{0u,2u})for(bool fused:{false,true}){GpuHiZCuller culler(device,directory);require(culler.prepareDepth(size.x,size.y,baseMip),"oracle pyramid allocation failed");culler.fusedBuild(fused);Resources scratch(device);auto commands=device->createCommandList();culler.recordDepthPyramid(scratch,commands,sourceView,"hiz/oracle-");device->submit(commands);culler.commit();
            auto expected=values;uint32_t width=size.x,height=size.y;
            for(uint32_t level=0;level<culler.pyramidLevels()+baseMip;++level){if(level>=baseMip){require(culler.readDepth(level-baseMip)==expected,"HiZ mip differs from independent exact max reduction");checkedMipTexels+=expected.size();}
                uint32_t w=std::max(width/2,1u),h=std::max(height/2,1u);std::vector<float> next(size_t(w)*h,0);
                for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){uint32_t endX=x==w-1?width:std::min(x*2+2,width),endY=y==h-1?height:std::min(y*2+2,height);
                    for(uint32_t yy=y*2;yy<endY;++yy)for(uint32_t xx=x*2;xx<endX;++xx)next[size_t(y)*w+x]=std::max(next[size_t(y)*w+x],expected[size_t(yy)*width+xx]);}
                expected=std::move(next);width=w;height=h;
            }
        }
    }
    // Cross the native 65,535 X-workgroup limit without a costly raster pass.
    {MeshPayload tiny;tiny.vertices={{{-1,0,0},{0,1,0},{0,0}},{{1,0,0},{0,1,0},{1,0}},{{0,0,1},{0,1,0},{0,1}}};tiny.indices={0,1,2};
        auto hierarchy=std::make_shared<ClusterHierarchy>();hierarchy->nodes.resize(1);hierarchy->nodes[0].lowError={-1,0,0,0};hierarchy->nodes[0].high={1,0,1,0};hierarchy->nodes[0].links.w=0;
        for(uint32_t i=0;i<65537;++i){GeometryCluster c;c.low={-1,0,0,0};c.high={1,0,1,0};c.range={i*3,3,0,0};hierarchy->clusters.push_back(c);hierarchy->indices.insert(hierarchy->indices.end(),{0,1,2});}
        hierarchy->sourceIndices=uint32_t(hierarchy->indices.size());hierarchy->leafClusters=65537;hierarchy->roots={0};
        DrawPacket packet{std::make_shared<GpuMesh>(device,tiny.vertices,tiny.indices),std::make_shared<GpuMaterial>(device,MaterialDesc{}),glm::mat4(1),65537};packet.clusters=std::make_shared<GpuClusterGeometry>(device,hierarchy);
        FrameData frame;frame.viewportWidth=64;frame.viewportHeight=64;frame.clusters.enabled=true;frame.objectCulling=false;Resources scratch(device);GpuClusterCuller culler(device,directory);auto commands=device->createCommandList();
        culler.record(scratch,commands,frame,{packet},std::vector<CulledMeshDraw>(1));device->submit(commands);culler.commit();require(culler.stats().batched,"large cluster grid fell back");require(culler.readIndices(0)==hierarchy->indices,"2D scatter index stream differs");require(culler.readCounts().at(0).selectedClusters==65537,"2D scatter cluster count differs");
    }
    validateClusterHierarchy();auto source=grid();auto hierarchy=buildClusterHierarchy(source);
    MaterialDesc desc;desc.parameters.albedoAlpha={.45f,.65f,.8f,1};desc.parameters.factors={0,.8f,1,0};desc.parameters.emissiveNormal.w=0;
    DrawPacket p{std::make_shared<GpuMesh>(device,source.vertices,source.indices),std::make_shared<GpuMaterial>(device,desc),glm::mat4(1),42};
    p.clusters=std::make_shared<GpuClusterGeometry>(device,hierarchy);
    FrameData f;f.viewportWidth=320;f.viewportHeight=180;f.cameraPosition={2,6,10};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;f.viewProjection=depth*glm::perspective(glm::radians(50.f),320.f/180,.1f,500.f)*f.view;
    f.nearPlane=.1f;f.farPlane=500;f.clusters.enabled=true;f.objectCulling=false;f.ambient=1;f.directionalEnabled=false;
    ForwardPbrRenderer renderer(device,directory,320,180);nlohmann::json results=nlohmann::json::array();
    std::vector<uint8_t> baseline;
    auto focal=180.f*.5f/std::tan(glm::radians(25.f));
    for(float budget:{0.f,.25f,1.f,4.f,64.f}){f.clusters.pixelError=budget;renderer.render(f,{p});auto gpu=renderer.readClusterIndices(0);
        auto cut=selectClusterNodes(*hierarchy,f.view,focal,1,.1f,budget);auto cpu=cutIndices(*hierarchy,cut);
        require(gpu==cpu,"GPU cut/index compaction differs from CPU reference");auto counts=renderer.readClusterCullCounts();require(counts.size()==1&&counts[0].selectedTriangles==gpu.size()/3,"GPU indirect counts");
        require(std::find(renderer.graphPassStats().executed.begin(),renderer.graphPassStats().executed.end(),"cluster-visibility")!=renderer.graphPassStats().executed.end(),"RDG cluster pass culled");
        auto pixels=renderer.readOutput();if(budget==0){baseline=pixels;auto off=f;off.clusters.enabled=false;renderer.render(off,{p});require(renderer.readOutput()==baseline,"zero-error cluster path changes pixels");}
        results.push_back({{"pixel_budget",budget},{"source_triangles",hierarchy->sourceIndices/3},{"selected_triangles",gpu.size()/3},{"cpu_gpu_indices_equal",true}});
    }
    // Native textured attribute seams: compare original raster and packed leaves,
    // including the retained corner tangent buffer and every G-buffer attachment.
    {ForwardPbrRenderer seamView(device,directory,320,180,PbrPath::Scene);auto smooth=grid(12);MeshPayload corners;
        for(size_t t=0;t<smooth.indices.size();t+=3)for(int k=0;k<3;++k){auto vertex=smooth.vertices[smooth.indices[t+k]];vertex.uv.x+=float(t)*.013f;
            corners.indices.push_back(uint32_t(corners.vertices.size()));corners.vertices.push_back(vertex);corners.pathTracingTangents.push_back({1,0,0,t%2?-1.f:1.f});}
        auto seamDesc=desc;seamDesc.images[0]={2,2,{255,32,8,255,8,255,32,255,32,8,255,255,255,255,8,255}};seamDesc.filter=rhi::Filter::Nearest;
        DrawPacket seam{std::make_shared<GpuMesh>(device,corners.vertices,corners.indices),std::make_shared<GpuMaterial>(device,seamDesc),glm::mat4(1),4242};seam.mesh->setMaterialTangents(corners.pathTracingTangents);
        seam.clusters=std::make_shared<GpuClusterGeometry>(device,buildClusterHierarchy(corners));auto original=f;original.clusters.enabled=false;seamView.render(original,{seam});auto hdr=seamView.readHDR(),depth=seamView.readDepth();auto pixels=seamView.readOutput();std::array<std::vector<float>,6> attachments;
        for(uint32_t a=0;a<6;++a)attachments[a]=seamView.readGBuffer(a);
        for(float budget:{0.f,64.f}){auto packed=f;packed.clusters.pixelError=budget;seamView.render(packed,{seam});require(seamView.readOutput()==pixels&&seamView.readDepth()==depth&&seamView.readHDR()==hdr,"native seam packing changed textured source pixels");
            for(uint32_t a=0;a<6;++a)require(seamView.readGBuffer(a)==attachments[a],"native seam packing changed a G-buffer attachment");}
    }
    // Protected cutout material still gets meshlet culling but must use leaves.
    auto alphaDesc=desc;alphaDesc.parameters.factors.w=.5f;auto alpha=p;alpha.material=std::make_shared<GpuMaterial>(device,alphaDesc);
    f.clusters.pixelError=64;renderer.render(f,{alpha});require(renderer.readClusterIndices(0).size()==source.indices.size(),"alpha cutout LOD protection");
    // Restore visibility without trusting a previous frame's selected count.
    f.objectCulling=true;auto moved=p;moved.model=glm::translate(glm::mat4(1),glm::vec3(1000,0,0));renderer.render(f,{moved});require(renderer.readClusterCullCounts().empty(),"offscreen object should be removed before clusters");
    renderer.render(f,{p});require(!renderer.readClusterIndices(0).empty(),"cluster visibility did not recover");
    // A narrow orthographic view intersects a large mesh. Test actual cluster
    // visibility against an independent double-precision 8-corner reference.
    auto narrow=f;narrow.clusters.pixelError=0;narrow.view=glm::lookAt(glm::vec3(0,10,0),glm::vec3(0),glm::vec3(0,0,-1));
    narrow.viewProjection=depth*glm::ortho(-1.f,1.f,-1.f,1.f,.1f,50.f)*narrow.view;
    renderer.render(narrow,{p});std::vector<uint32_t> visible;
    for(const auto& c:hierarchy->clusters)if(hierarchy->nodes[c.range.z].links.y==UINT32_MAX){
        std::array<bool,6> outside;outside.fill(true);
        for(int corner=0;corner<8;++corner){glm::dvec4 q=glm::dmat4(narrow.viewProjection)*glm::dvec4(corner&1?c.high.x:c.low.x,corner&2?c.high.y:c.low.y,corner&4?c.high.z:c.low.z,1);
            const double x=1+4./320,y=1+4./180;std::array<double,6> distances{q.x+q.w*x,q.w*x-q.x,q.y+q.w*y,q.w*y-q.y,q.z,q.w-q.z};
            for(int k=0;k<6;++k)outside[k]=outside[k]&&distances[k]<-1e-5;
        }
        if(std::find(outside.begin(),outside.end(),true)==outside.end())visible.insert(visible.end(),hierarchy->indices.begin()+c.range.x,hierarchy->indices.begin()+c.range.x+c.range.y);
    }
    require(renderer.readClusterIndices(0)==visible&&visible.size()<hierarchy->sourceIndices,"cluster frustum compaction mismatch");
    f.objectCulling=false;
    // Repeat the independent cut comparison with shear, mirror and nonuniform scale.
    auto model=glm::scale(glm::mat4(1),glm::vec3(-1.2f,.7f,1));model[1][0]=.15f;p.model=model;f.objectCulling=false;renderer.render(f,{p});
    float one=0,inf=0;for(int i=0;i<3;++i){float a=0,b=0;for(int j=0;j<3;++j){a+=std::abs(model[i][j]);b+=std::abs(model[j][i]);}one=std::max(one,a);inf=std::max(inf,b);}
    require(renderer.readClusterIndices(0)==cutIndices(*hierarchy,selectClusterNodes(*hierarchy,f.view*model,focal,std::sqrt(one*inf),.1f,64)),"scaled GPU cut mismatch");
    auto orthographic=f;orthographic.viewProjection=depth*glm::ortho(-8.f,8.f,-8.f,8.f,.1f,500.f)*f.view;orthographic.clusters.pixelError=4;
    renderer.render(orthographic,{p});
    require(renderer.readClusterIndices(0)==cutIndices(*hierarchy,selectClusterNodes(*hierarchy,f.view*model,20,std::sqrt(one*inf),.1f,4,true)),"orthographic GPU cut mismatch");
    // Multiple meshes share one output buffer but retain local vertex indices,
    // independent transforms, argument firstIndex and per-mesh prefix scans.
    std::vector<DrawPacket> many{p,p,p};for(size_t i=0;i<many.size();++i){many[i].id=100+i;many[i].model=glm::translate(glm::mat4(1),glm::vec3(float(i)*.2f,0,0));}
    auto batchFrame=f;batchFrame.clusters.pixelError=4;batchFrame.objectCulling=false;batchFrame.clusters.batched=false;
    renderer.render(batchFrame,many);auto oldPixels=renderer.readOutput();std::vector<std::vector<uint32_t>> oldIndices;
    for(size_t i=0;i<many.size();++i)oldIndices.push_back(renderer.readClusterIndices(i));
    require(renderer.clusterCullStats().dispatches==9,"legacy fixture dispatch count");
    batchFrame.clusters.batched=true;renderer.render(batchFrame,many);
    require(renderer.clusterCullStats().batched&&renderer.clusterCullStats().dispatches==3,"mesh batch did not reduce dispatches");
    for(size_t i=0;i<many.size();++i)require(renderer.readClusterIndices(i)==oldIndices[i],"batch changed a mesh's local indices");
    require(renderer.readOutput()==oldPixels,"batch changed display pixels");
    renderer.render(batchFrame,{many[2],many[0]});require(renderer.readClusterIndices(0)==oldIndices[2]&&renderer.readClusterIndices(1)==oldIndices[0],"cached batch reordered/hidden mesh mapping");
    renderer.render(batchFrame,many);for(size_t i=0;i<many.size();++i)require(renderer.readClusterIndices(i)==oldIndices[i],"batch visibility recovery");
    auto duplicateIds=many;for(auto& draw:duplicateIds)draw.id=0;renderer.render(batchFrame,duplicateIds);
    for(size_t i=0;i<duplicateIds.size();++i)require(renderer.readClusterIndices(i)==oldIndices[i],"duplicate IDs overwrote a shared mesh's transform");
    require(renderer.readOutput()==oldPixels,"duplicate-ID batch changed pixels");
    auto changed=grid(32);many[1].mesh=std::make_shared<GpuMesh>(device,changed.vertices,changed.indices);many[1].clusters=std::make_shared<GpuClusterGeometry>(device,buildClusterHierarchy(changed));
    batchFrame.clusters.pixelError=0;renderer.render(batchFrame,many);require(renderer.readClusterIndices(1).size()==changed.indices.size(),"batch retained a stale geometry revision");

    // One mesh spans both hidden and exposed components; whole-object HiZ
    // cannot remove it, whereas meshlet tests must remove only the hidden part.
    auto plane=[](MeshPayload& m,float cx,float radius,float z,uint32_t n){uint32_t base=uint32_t(m.vertices.size());
        for(uint32_t y=0;y<=n;++y)for(uint32_t x=0;x<=n;++x)m.vertices.push_back({{cx-radius+2*radius*x/n,-radius+2*radius*y/n,z},{0,0,1},{float(x)/n,float(y)/n}});
        for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x){auto a=base+y*(n+1)+x,b=a+1,c=a+n+1,d=c+1;m.indices.insert(m.indices.end(),{a,b,c,b,d,c});}
    };
    auto packet=[&](const MeshPayload& m,uint64_t id){DrawPacket q{std::make_shared<GpuMesh>(device,m.vertices,m.indices),std::make_shared<GpuMaterial>(device,desc),glm::mat4(1),id};q.clusters=std::make_shared<GpuClusterGeometry>(device,buildClusterHierarchy(m));return q;};
    MeshPayload front,back;plane(front,0,.6f,.2f,16);plane(back,0,.15f,.7f,16);plane(back,.85f,.1f,.7f,16);
    std::vector<DrawPacket> layered{packet(front,500),packet(back,501)};
    FrameData hz;hz.viewportWidth=129;hz.viewportHeight=97;hz.ambient=1;hz.directionalEnabled=false;hz.clusters.enabled=true;hz.clusters.pixelError=0;hz.clusters.twoPhase=false;
    hz.hizMaxOccluders=1;hz.hizOccluderMinArea=.1f;hz.objectCulling=false;
    ForwardPbrRenderer occlusionView(device,directory,129,97,PbrPath::Scene);
    uint32_t removed=0;float worst=0;size_t comparisons=0;
    auto compareOcclusion=[&](const std::vector<DrawPacket>& draws,const FrameData& base){auto test=base;test.clusters.occlusion=false;test.hizCulling=false;
        occlusionView.render(test,draws);auto reference=occlusionView.readHDR();auto pixels=occlusionView.readOutput();auto counts=occlusionView.readClusterCullCounts();
        test.clusters.occlusion=true;occlusionView.render(test,draws);auto actual=occlusionView.readHDR();
        for(size_t i=0;i<actual.size();++i){require(std::isfinite(actual[i]),"cluster HiZ non-finite HDR");worst=std::max(worst,std::abs(reference[i]-actual[i]));require(std::abs(reference[i]-actual[i])<.001f,"cluster HiZ changed visible HDR");}
        require(pixels==occlusionView.readOutput(),"cluster HiZ changed displayed surface");++comparisons;return occlusionView.readClusterCullCounts();
    };
    auto visibleCounts=compareOcclusion(layered,hz);
    require(occlusionView.clusterCullStats().occlusion&&occlusionView.clusterCullStats().dispatches==6,"cluster HiZ did not execute two batched cuts");
    require(visibleCounts.size()==2&&visibleCounts[1].selectedTriangles>0&&visibleCounts[1].selectedTriangles<back.indices.size()/3,"cluster HiZ failed partial-mesh occlusion");
    removed=uint32_t(back.indices.size()/3-visibleCounts[1].selectedTriangles);
    auto moving=layered;moving[0].model=glm::translate(glm::mat4(1),glm::vec3(2,0,0));visibleCounts=compareOcclusion(moving,hz);
    require(visibleCounts[1].selectedTriangles==back.indices.size()/3,"cluster HiZ reveal reused stale visibility");
    auto equal=layered;equal[1].model=glm::translate(glm::mat4(1),glm::vec3(0,0,-.5f));visibleCounts=compareOcclusion(equal,hz);
    require(visibleCounts[1].selectedTriangles==back.indices.size()/3,"cluster HiZ equal-depth rejection");
    equal[1].model=glm::translate(glm::mat4(1),glm::vec3(0,0,-.7f));visibleCounts=compareOcclusion(equal,hz);
    require(visibleCounts[1].selectedTriangles==back.indices.size()/3,"cluster HiZ near-depth rejection");
    auto masked=layered;auto mask=desc;mask.parameters.factors.w=.5f;mask.images[0]={2,1,{255,255,255,255,255,255,255,0}};mask.filter=rhi::Filter::Nearest;masked[0].material=std::make_shared<GpuMaterial>(device,mask);
    compareOcclusion(masked,hz);
    auto reflected=layered;reflected[1].model=glm::scale(glm::mat4(1),glm::vec3(-1,1,1));reflected[1].model[1][0]=.1f;compareOcclusion(reflected,hz);
    occlusionView.resize(65,33);auto small=hz;small.viewportWidth=65;small.viewportHeight=33;compareOcclusion(layered,small);
    occlusionView.resize(129,97);auto coarse=hz;coarse.clusters.pixelError=64;compareOcclusion(layered,coarse);
    auto perspectiveHz=hz;glm::mat4 depthRemap(1);depthRemap[2][2]=.5f;depthRemap[3][2]=.5f;
    perspectiveHz.viewProjection=depthRemap*glm::perspective(glm::radians(70.f),129.f/97.f,.1f,100.f);
    auto perspectiveLayers=layered;perspectiveLayers[0].model=glm::translate(glm::mat4(1),glm::vec3(0,0,-2.2f));perspectiveLayers[1].model=glm::translate(glm::mat4(1),glm::vec3(0,0,-3.7f));compareOcclusion(perspectiveLayers,perspectiveHz);
    perspectiveLayers[1].model=glm::translate(glm::mat4(1),glm::vec3(0,0,-.75f));compareOcclusion(perspectiveLayers,perspectiveHz);
    auto crossing=back;for(auto& v:crossing.vertices)v.position.z=-.2f+v.position.y*2;
    perspectiveLayers[1]=packet(crossing,503);compareOcclusion(perspectiveLayers,perspectiveHz);
    auto legacyHz=hz;legacyHz.clusters.batched=false;legacyHz.clusters.occlusion=true;occlusionView.render(legacyHz,layered);
    require(!occlusionView.clusterCullStats().occlusion,"legacy fallback reported cluster occlusion");
    // Independent reference renderer keeps temporal tests from accidentally
    // invalidating their own history by toggling occlusion for the reference.
    ForwardPbrRenderer temporalView(device,directory,129,97,PbrPath::Scene),referenceView(device,directory,129,97,PbrPath::Scene);
    auto temporalFrame=hz;temporalFrame.clusters.twoPhase=true;temporalFrame.clusters.occlusion=true;
    nlohmann::json temporalCases=nlohmann::json::array();uint64_t recovered=0;float temporalWorst=0;
    auto compareTemporal=[&](const char* name,const std::vector<DrawPacket>& draws,const FrameData& f){
        auto ref=f;ref.clusters.occlusion=false;ref.hizCulling=false;
        referenceView.render(ref,draws);auto hdr=referenceView.readHDR();auto display=referenceView.readOutput();
        temporalView.render(f,draws);auto actual=temporalView.readHDR();
        require(temporalView.clusterCullStats().twoPhase,"two-stage fallback unexpectedly active");
        for(size_t i=0;i<hdr.size();++i){require(std::isfinite(actual[i]),"two-stage nonfinite HDR");temporalWorst=std::max(temporalWorst,std::abs(hdr[i]-actual[i]));require(std::abs(hdr[i]-actual[i])<.001f,"two-stage changed visible HDR");}
        require(display==temporalView.readOutput(),"two-stage changed display pixels");
        // Lighting can hide a missing surface: also compare all six raw G-buffer attachments.
        for(uint32_t a=0;a<6;++a){auto expected=referenceView.readGBuffer(a),got=temporalView.readGBuffer(a);require(expected.size()==got.size(),"two-stage attachment size");
            for(size_t i=0;i<got.size();++i)require(std::isfinite(got[i])&&std::abs(expected[i]-got[i])<.001f,"two-stage changed G-buffer surface");}
        uint64_t selected=0,early=0,late=0,rejected=0;auto flags=temporalView.readClusterVisibility();
        for(const auto& c:flags){require(!c.early||c.selected,"early outside current cut");require(!c.late||(c.selected&&!c.early),"late duplicated early cluster");
            if(c.selected)selected+=c.indexCount/3;if(c.early)early+=c.indexCount/3;if(c.late)late+=c.indexCount/3;if(c.selected&&!c.early&&!c.late)rejected+=c.indexCount/3;}
        for(size_t packet=0;packet<draws.size();++packet)if(draws[packet].clusters){std::vector<uint32_t> expectedIndices;
            const auto& h=*draws[packet].clusters->hierarchy;
            for(bool latePass:{false,true})for(auto c:flags)if(c.packet==packet&&(latePass?c.late:c.early)){const auto& cluster=h.clusters[c.cluster];expectedIndices.insert(expectedIndices.end(),h.indices.begin()+cluster.range.x,h.indices.begin()+cluster.range.x+cluster.range.y);}
            require(temporalView.readClusterIndices(packet)==expectedIndices,"two-stage indirect stream differs from stage membership");}
        uint64_t output=0;for(auto c:temporalView.readClusterCullCounts())output+=c.selectedTriangles;
        require(output==early+late&&selected==output+rejected,"two-stage cut partition lost or duplicated indices");
        temporalCases.push_back({{"case",name},{"history_used",temporalView.clusterCullStats().historyUsed},{"cut_triangles",selected},{"early_triangles",early},{"late_triangles",late},{"rejected_triangles",rejected},{"hdr_display_gbuffer_equal",true}});recovered+=late;
        return late;
    };
    compareTemporal("cold",layered,temporalFrame);require(!temporalView.clusterCullStats().historyUsed,"cold frame consumed history");
    compareTemporal("warm",layered,temporalFrame);require(temporalCases.back().at("rejected_triangles").get<uint64_t>()>0,"warm history rejected no hidden clusters");
    require(compareTemporal("occluder-moves",moving,temporalFrame)>0,"disoccluded surface was not recovered late");
    compareTemporal("occluder-returns",layered,temporalFrame);
    require(compareTemporal("alpha-hole-opens",masked,temporalFrame)>0,"alpha hole did not recover hidden surface");
    compareTemporal("equal-depth",equal,temporalFrame);
    compareTemporal("mirror-shear",reflected,temporalFrame);
    compareTemporal("coarse-current-cut",layered,[&]{auto f=temporalFrame;f.clusters.pixelError=64;return f;}());
    compareTemporal("fine-current-cut",layered,temporalFrame);
    auto shifted=temporalFrame;shifted.viewProjection=glm::translate(glm::mat4(1),glm::vec3(.4f,.1f,0));compareTemporal("camera-pan",layered,shifted);
    compareTemporal("camera-return",layered,temporalFrame);
    auto persp=perspectiveHz;persp.clusters.twoPhase=true;persp.clusters.occlusion=true;compareTemporal("camera-cut-perspective",perspectiveLayers,persp);
    auto nearLayers=layered;nearLayers[1].model=glm::translate(glm::mat4(1),glm::vec3(0,0,-.7f));compareTemporal("near-plane",nearLayers,temporalFrame);
    auto removedFront=layered;removedFront.erase(removedFront.begin());compareTemporal("occluder-removal",removedFront,temporalFrame);
    compareTemporal("mesh-return",layered,temporalFrame);
    temporalView.resize(65,33);referenceView.resize(65,33);auto smaller=temporalFrame;smaller.viewportWidth=65;smaller.viewportHeight=33;compareTemporal("odd-resize",layered,smaller);
    require(!temporalView.clusterCullStats().historyUsed,"resized history was consumed");compareTemporal("odd-resize-warm",layered,smaller);
    temporalView.resetTemporal();compareTemporal("explicit-reset",layered,smaller);require(!temporalView.clusterCullStats().historyUsed,"reset consumed history");
    auto duplicates=layered;duplicates[0].id=0;duplicates[1].id=0;compareTemporal("duplicate-ids",duplicates,smaller);compareTemporal("duplicate-ids-warm",duplicates,smaller);
    auto revision=layered;auto changedBack=back;for(auto& v:changedBack.vertices)v.position.x-=.1f;revision[1]=packet(changedBack,501);compareTemporal("geometry-revision",revision,smaller);
    auto disabled=smaller;disabled.clusters.enabled=false;temporalView.render(disabled,layered);compareTemporal("toggle-reenable",layered,smaller);require(!temporalView.clusterCullStats().historyUsed,"disabled clusters retained history");
    auto unbatched=smaller;unbatched.clusters.batched=false;temporalView.render(unbatched,layered);compareTemporal("legacy-reenable",layered,smaller);require(!temporalView.clusterCullStats().historyUsed,"legacy path retained history");
    // No clustered geometry must cleanly bypass external-depth preparation.
    auto ordinary=layered;for(auto& p:ordinary)p.clusters.reset();temporalView.render(smaller,ordinary);require(!temporalView.clusterCullStats().twoPhase,"unclustered scene reports two-stage");
    compareTemporal("hierarchy-ready",layered,smaller);require(!temporalView.clusterCullStats().historyUsed,"unclustered scene retained history");
    auto incompatible=layered;for(auto& p:incompatible)p.wireframe=true;
    temporalView.render(smaller,incompatible);auto noOcclusion=smaller;noOcclusion.clusters.occlusion=false;referenceView.render(noOcclusion,incompatible);
    require(!temporalView.clusterCullStats().twoPhase&&!temporalView.hizCullStats().active,"unsupported packet fallback built unused HiZ");
    require(temporalView.readOutput()==referenceView.readOutput(),"unsupported packet fallback changed image");
    temporalCases.push_back({{"case","unsupported-packet-fallback"},{"no_unused_hiz",true},{"display_equal",true}});
    temporalView.resetTemporal();referenceView.resetTemporal();auto jittered=smaller;jittered.taa=true;
    for(int i=0;i<6;++i)compareTemporal(("taa-jitter-"+std::to_string(i)).c_str(),layered,jittered);
    nlohmann::json report={{"scope","actual GPU indices compared byte-for-byte with independent CPU hierarchical cut"},{"grid",results},{"zero_error_pixels_equal",true},{"alpha_protected",true},{"visibility_recovery",true},{"shear_mirror_scale",true},{"cluster_frustum_reference",true},{"orthographic_reference",true},{"batch_indices_and_pixels_equal",true},{"batch_dispatches_before",9},{"batch_dispatches_after",3},{"cluster_hiz",{{"comparisons",comparisons},{"hidden_triangles_removed",removed},{"hdr_max_error",worst},{"display_equal",true},{"same_frame_lod_depth",true}}}};
    report["scatter_2d"]={{"clusters",65537},{"indices_exact",true}};
    report["seam_split_gpu_exact"]=true;
    report["pyramid_oracle"]={{"builders",{"single_level","three_level_fused"}},{"base_mips",{0,2}},{"sizes",8},{"checked_texels",checkedMipTexels},{"exact",true}};
    report["two_stage"]={{"cases",temporalCases},{"recovered_triangles",recovered},{"hdr_max_error",temporalWorst},{"six_gbuffer_attachments_compared",true}};
    const char* capture=std::getenv("SCENERENDERER_CLUSTER_CAPTURE_DIR");
    if(capture){std::filesystem::create_directories(capture);std::ofstream out(std::filesystem::path(capture)/"validation.json");out<<report.dump(2)<<'\n';}
    std::cout<<report.dump(2)<<'\n'<<"GPU cluster rendering validation passed\n";
}
}
