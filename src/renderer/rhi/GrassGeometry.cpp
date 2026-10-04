#include "renderer/rhi/GrassGeometry.h"
#include "renderer/rhi/SceneSnapshot.h"
#include <cmath>
namespace render {
std::shared_ptr<const MeshPayload> grassBladeGeometry(){auto mesh=std::make_shared<MeshPayload>();
    for(int blade=0;blade<4;++blade){float a=blade*2.3999632f,c=std::cos(a),s=std::sin(a);const glm::vec3 p[]={{-.015f,0,0},{.015f,0,0},{-.012f,.08f,.004f},{.012f,.08f,.004f},{-.007f,.17f,.025f},{.007f,.17f,.025f},{0,.25f,.055f}};const glm::vec2 uv[]={{0,1},{1,1},{0,.68f},{1,.68f},{0,.32f},{1,.32f},{.5f,0}};uint32_t base=uint32_t(mesh->vertices.size());for(int i=0;i<7;++i)mesh->vertices.push_back({{c*p[i].x+s*p[i].z,p[i].y*(.85f+.1f*blade),-s*p[i].x+c*p[i].z},{s,0,c},uv[i]});for(uint32_t i:{0u,2u,1u,1u,2u,3u,2u,4u,3u,3u,4u,5u,4u,6u,5u})mesh->indices.push_back(base+i);}return mesh;
}
}
